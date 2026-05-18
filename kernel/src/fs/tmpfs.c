#include <fs/vfs.h>

#include <lib/string.h>
#include <mm/kheap.h>
#include <sys/time.h>
#define KLOG_NS "tmpfs"
#include <log/klog.h>

typedef struct tmpfs_node tmpfs_node_t;
typedef struct tmpfs_vfs tmpfs_vfs_t;

struct tmpfs_node {
	vnode_t vnode;
	tmpfs_node_t *parent;
	tmpfs_node_t *next_sibling;
	tmpfs_node_t *first_child;
	char *name;
	char *data;
	size_t capacity;
	size_t size;
	mode_t mode;
	ino_t inode;
	timespec_t atime;
	timespec_t mtime;
	timespec_t ctime;
};

struct tmpfs_vfs {
	vfs_t vfs;
	tmpfs_node_t *root;
	ino_t next_inode;
	size_t node_count;
	size_t byte_count;
};

static int tmpfs_mount(vfs_t **vfs, vnode_t *mountpoint, vnode_t *backing,
					   void *data);
static int tmpfs_unmount(vfs_t *vfs);
static int tmpfs_sync_fs(vfs_t *vfs);
static int tmpfs_root(vfs_t *vfs, vnode_t **root);
static int tmpfs_statfs(vfs_t *vfs, fsattr_t *fsattr);

static int tmpfs_open(vnode_t **node, int flags, cred_t *cred);
static int tmpfs_close(vnode_t *node, int flags, cred_t *cred);
static int tmpfs_read(vnode_t *node, void *buffer, size_t size,
					  uintmax_t offset, int flags, size_t *readc, cred_t *cred);
static int tmpfs_write(vnode_t *node, const void *buffer, size_t size,
					   uintmax_t offset, int flags, size_t *writec, cred_t *cred);
static int tmpfs_lookup(vnode_t *node, char *name, vnode_t **result,
						cred_t *cred);
static int tmpfs_create(vnode_t *parent, char *name, vattr_t *attr, mode_t type,
						vnode_t **result, cred_t *cred);
static int tmpfs_getdents(vnode_t *node, dirent_t *buffer, size_t size,
						  uintmax_t offset, size_t *readc, cred_t *cred);
static void tmpfs_inactive(vnode_t *node);
static int tmpfs_sync_node(vnode_t *node);

static vfsops_t tmpfs_vfsops = {
	.mount = tmpfs_mount,
	.unmount = tmpfs_unmount,
	.sync = tmpfs_sync_fs,
	.root = tmpfs_root,
	.statfs = tmpfs_statfs,
};

static vops_t tmpfs_vops = {
	.open = tmpfs_open,
	.close = tmpfs_close,
	.read = tmpfs_read,
	.write = tmpfs_write,
	.lookup = tmpfs_lookup,
	.create = tmpfs_create,
	.getdents = tmpfs_getdents,
	.inactive = tmpfs_inactive,
	.sync = tmpfs_sync_node,
};

static bool tmpfs_open_flags_valid(int flags)
{
	switch (flags & O_ACCMODE) {
	case O_RDONLY:
	case O_WRONLY:
	case O_RDWR:
		return true;
	default:
		return false;
	}
}

static bool tmpfs_name_equal(const char *lhs, const char *rhs)
{
	size_t lhs_len;
	size_t rhs_len;

	if (!lhs || !rhs)
		return false;

	lhs_len = strlen(lhs);
	rhs_len = strlen(rhs);
	return lhs_len == rhs_len && memcmp(lhs, rhs, lhs_len) == 0;
}

static size_t tmpfs_min_size(size_t a, size_t b)
{
	return a < b ? a : b;
}

static uint8_t tmpfs_dirent_type(mode_t mode)
{
	if (S_ISDIR(mode))
		return DT_DIR;
	if (S_ISREG(mode))
		return DT_REG;
	if (S_ISCHR(mode))
		return DT_CHR;
	if (S_ISBLK(mode))
		return DT_BLK;
	if (S_ISFIFO(mode))
		return DT_FIFO;
	if (S_ISLNK(mode))
		return DT_LNK;
	if (S_ISSOCK(mode))
		return DT_SOCK;
	return DT_UNKNOWN;
}

static timespec_t tmpfs_now(void)
{
	uint64_t uptime_us = time_uptime_us();
	timespec_t ts;

	ts.s = (time_t)(uptime_us / 1000000);
	ts.ns = (time_t)((uptime_us % 1000000) * 1000);
	return ts;
}

static char *tmpfs_strdup(const char *src)
{
	size_t len;
	char *dup;

	if (!src)
		return NULL;

	len = strlen(src) + 1;
	dup = kmalloc(len);
	if (!dup)
		return NULL;

	memcpy(dup, src, len);
	return dup;
}

static tmpfs_vfs_t *tmpfs_from_vfs(vfs_t *vfs)
{
	return (tmpfs_vfs_t *)vfs;
}

static tmpfs_node_t *tmpfs_from_vnode(vnode_t *node)
{
	if (!node)
		return NULL;

	return (tmpfs_node_t *)node->private_data;
}

static tmpfs_node_t *tmpfs_find_child(tmpfs_node_t *parent, const char *name)
{
	tmpfs_node_t *child;

	for (child = parent->first_child; child; child = child->next_sibling) {
		if (tmpfs_name_equal(child->name, name))
			return child;
	}

	return NULL;
}

static tmpfs_node_t *tmpfs_child_at(tmpfs_node_t *parent, size_t index)
{
	tmpfs_node_t *child = parent->first_child;

	while (child && index != 0) {
		child = child->next_sibling;
		index--;
	}

	return child;
}

static void tmpfs_fill_dirent(dirent_t *entry, tmpfs_node_t *node,
							  const char *name, off_t next_offset)
{
	memset(entry, 0, sizeof(*entry));
	entry->d_ino = node ? node->inode : 0;
	entry->d_off = next_offset;
	entry->d_reclen = sizeof(*entry);
	entry->d_type = tmpfs_dirent_type(node ? node->mode : 0);
	strlcpy(entry->d_name, name ? name : "", sizeof(entry->d_name));
}

static int tmpfs_resize_node(tmpfs_vfs_t *fs, tmpfs_node_t *node, size_t size)
{
	char *new_data;
	size_t new_capacity;

	if (size <= node->capacity) {
		if (size > node->size)
			memset(node->data + node->size, 0, size - node->size);
		fs->byte_count += size - node->size;
		node->size = size;
		return 0;
	}

	new_capacity = node->capacity ? node->capacity : 64;
	while (new_capacity < size)
		new_capacity <<= 1;

	new_data = krealloc(node->data, new_capacity);
	if (!new_data)
		return -1;

	if (new_capacity > node->capacity)
		memset(new_data + node->capacity, 0, new_capacity - node->capacity);
	if (size > node->size)
		memset(new_data + node->size, 0, size - node->size);

	node->data = new_data;
	node->capacity = new_capacity;
	fs->byte_count += size - node->size;
	node->size = size;
	return 0;
}

static tmpfs_node_t *tmpfs_alloc_node(tmpfs_vfs_t *fs, tmpfs_node_t *parent,
									  const char *name, mode_t type, mode_t mode)
{
	tmpfs_node_t *node;
	timespec_t now;

	node = kzalloc(sizeof(*node));
	if (!node)
		return NULL;

	node->name = tmpfs_strdup(name ? name : "");
	if (!node->name) {
		kfree(node);
		return NULL;
	}

	vnode_init(&node->vnode, &tmpfs_vops, 0, type & S_IFMT, &fs->vfs);
	node->vnode.private_data = node;
	node->parent = parent;
	node->mode = (mode & ~S_IFMT) | (type & S_IFMT);
	node->inode = fs->next_inode++;
	now = tmpfs_now();
	node->atime = now;
	node->mtime = now;
	node->ctime = now;
	fs->node_count++;

	return node;
}

static void tmpfs_link_child(tmpfs_node_t *parent, tmpfs_node_t *child)
{
	child->next_sibling = parent->first_child;
	parent->first_child = child;
	parent->mtime = tmpfs_now();
	parent->ctime = parent->mtime;
	ktrace("linked child '%s' under parent inode=%llu", child->name,
		   (unsigned long long)parent->inode);
}

static int tmpfs_mount(vfs_t **vfs, vnode_t *mountpoint, vnode_t *backing,
					   void *data)
{
	tmpfs_vfs_t *fs;
	tmpfs_node_t *root;

	(void)mountpoint;
	(void)backing;
	(void)data;

	if (!vfs)
		return -1;

	fs = kzalloc(sizeof(*fs));
	if (!fs)
		return -1;

	vfs_struct_init(&fs->vfs, &tmpfs_vfsops, 0);
	fs->next_inode = 1;

	root = tmpfs_alloc_node(fs, NULL, "", S_IFDIR, 0755);
	if (!root) {
		kfree(fs);
		return -1;
	}

	fs->root = root;
	fs->vfs.root = &root->vnode;
	*vfs = &fs->vfs;
	klog("mounted tmpfs root inode=%llu", (unsigned long long)root->inode);
	return 0;
}

static int tmpfs_unmount(vfs_t *vfs)
{
	(void)vfs;
	return -1;
}

static int tmpfs_sync_fs(vfs_t *vfs)
{
	(void)vfs;
	return 0;
}

static int tmpfs_root(vfs_t *vfs, vnode_t **root)
{
	tmpfs_vfs_t *fs;

	if (!vfs || !root)
		return -1;

	fs = tmpfs_from_vfs(vfs);
	*root = &fs->root->vnode;
	vnode_hold(*root);
	return 0;
}

static int tmpfs_statfs(vfs_t *vfs, fsattr_t *fsattr)
{
	tmpfs_vfs_t *fs;

	if (!vfs || !fsattr)
		return -1;

	fs = tmpfs_from_vfs(vfs);
	memset(fsattr, 0, sizeof(*fsattr));
	fsattr->io_size = 4096;
	fsattr->block_size = 4096;
	fsattr->block_count = (fs->byte_count + 4095) / 4096;
	fsattr->inode_count = fs->node_count;
	fsattr->max_name_size = 255;
	return 0;
}

static int tmpfs_open(vnode_t **node, int flags, cred_t *cred)
{
	(void)node;
	(void)flags;
	(void)cred;

	if (!node || !*node || !tmpfs_open_flags_valid(flags))
		return -1;

	if ((flags & O_DIRECTORY) && !S_ISDIR((*node)->type))
		return -1;

	if (S_ISDIR((*node)->type) && ((flags & O_ACCMODE) != O_RDONLY))
		return -1;

	if ((flags & O_TRUNC) && S_ISREG((*node)->type) &&
		(flags & O_ACCMODE) != O_RDONLY) {
		tmpfs_node_t *tnode = tmpfs_from_vnode(*node);
		tmpfs_vfs_t *fs = tmpfs_from_vfs((*node)->vfs);

		mutex_lock(&(*node)->size_lock);
		fs->byte_count -= tnode->size;
		tnode->size = 0;
		tnode->mtime = tmpfs_now();
		tnode->ctime = tnode->mtime;
		mutex_unlock(&(*node)->size_lock);
	}

	ktrace("open vnode=%p flags=0x%x mode=0%o", *node, flags,
		   (unsigned int)tmpfs_from_vnode(*node)->mode);
	return 0;
}

static int tmpfs_close(vnode_t *node, int flags, cred_t *cred)
{
	(void)node;
	(void)flags;
	(void)cred;
	ktrace("close vnode=%p", node);
	return 0;
}

static int tmpfs_read(vnode_t *node, void *buffer, size_t size,
					  uintmax_t offset, int flags, size_t *readc, cred_t *cred)
{
	tmpfs_node_t *tnode;
	size_t available;
	size_t count;

	(void)flags;
	(void)cred;

	if (!node || !buffer)
		return -1;

	tnode = tmpfs_from_vnode(node);
	if (!tnode || !S_ISREG(node->type))
		return -1;

	mutex_lock(&node->size_lock);
	if ((size_t)offset >= tnode->size) {
		count = 0;
	} else {
		available = tnode->size - (size_t)offset;
		count = tmpfs_min_size(size, available);
		memcpy(buffer, tnode->data + offset, count);
	}
	tnode->atime = tmpfs_now();
	mutex_unlock(&node->size_lock);

	if (readc)
		*readc = count;
	ktrace("read inode=%llu size=%zu offset=%ju copied=%zu",
		   (unsigned long long)tnode->inode, size, (uintmax_t)offset, count);
	return 0;
}

static int tmpfs_write(vnode_t *node, const void *buffer, size_t size,
					   uintmax_t offset, int flags, size_t *writec, cred_t *cred)
{
	tmpfs_node_t *tnode;
	tmpfs_vfs_t *fs;
	size_t end;
	int rc;

	(void)flags;
	(void)cred;

	if (!node || (!buffer && size != 0))
		return -1;

	tnode = tmpfs_from_vnode(node);
	if (!tnode || !S_ISREG(node->type))
		return -1;

	fs = tmpfs_from_vfs(node->vfs);
	if (flags & O_APPEND)
		offset = tnode->size;
	end = (size_t)offset + size;

	mutex_lock(&node->size_lock);
	rc = tmpfs_resize_node(fs, tnode, end);
	if (rc == 0 && size != 0)
		memcpy(tnode->data + offset, buffer, size);
	if (rc == 0) {
		tnode->mtime = tmpfs_now();
		tnode->ctime = tnode->mtime;
	}
	mutex_unlock(&node->size_lock);

	if (rc != 0)
		return rc;

	if (writec)
		*writec = size;
	ktrace("write inode=%llu size=%zu offset=%ju", (unsigned long long)tnode->inode,
		   size, (uintmax_t)offset);
	return 0;
}

static int tmpfs_lookup(vnode_t *node, char *name, vnode_t **result,
						cred_t *cred)
{
	tmpfs_node_t *parent;
	tmpfs_node_t *child;

	(void)cred;

	if (!node || !name || !result)
		return -1;

	if (tmpfs_name_equal(name, "."))
		child = tmpfs_from_vnode(node);
	else if (tmpfs_name_equal(name, "..")) {
		parent = tmpfs_from_vnode(node);
		child = parent && parent->parent ? parent->parent : parent;
	} else {
		parent = tmpfs_from_vnode(node);
		if (!parent || !S_ISDIR(node->type))
			return -1;

		mutex_lock(&node->lock);
		child = tmpfs_find_child(parent, name);
		mutex_unlock(&node->lock);
	}

	if (!child)
		return -1;

	*result = &child->vnode;
	ktrace("lookup name='%s' -> inode=%llu", name,
		   (unsigned long long)child->inode);
	return 0;
}

static int tmpfs_create(vnode_t *parent_vnode, char *name, vattr_t *attr,
						mode_t type, vnode_t **result, cred_t *cred)
{
	tmpfs_node_t *parent;
	tmpfs_node_t *child;
	tmpfs_vfs_t *fs;
	mode_t mode = DEFFILEMODE;

	(void)cred;

	if (!parent_vnode || !name || !*name || !result)
		return -1;

	type &= S_IFMT;
	if (type != S_IFREG && type != S_IFDIR)
		return -1;

	parent = tmpfs_from_vnode(parent_vnode);
	if (!parent || !S_ISDIR(parent_vnode->type))
		return -1;

	if (attr)
		mode = attr->mode & ALLPERMS;
	if (type == S_IFDIR && attr == NULL)
		mode = 0777;
	if (strlen(name) > NAME_MAX)
		return -1;

	mutex_lock(&parent_vnode->lock);
	if (tmpfs_find_child(parent, name)) {
		mutex_unlock(&parent_vnode->lock);
		return -1;
	}

	fs = tmpfs_from_vfs(parent_vnode->vfs);
	child = tmpfs_alloc_node(fs, parent, name, type, mode);
	if (!child) {
		mutex_unlock(&parent_vnode->lock);
		return -1;
	}

	tmpfs_link_child(parent, child);
	mutex_unlock(&parent_vnode->lock);

	*result = &child->vnode;
	klog("created %s '%s' inode=%llu mode=0%o", type == S_IFDIR ? "dir" : "file",
		 name, (unsigned long long)child->inode, (unsigned int)child->mode);
	return 0;
}

static int tmpfs_getdents(vnode_t *node, dirent_t *buffer, size_t size,
						  uintmax_t offset, size_t *readc, cred_t *cred)
{
	tmpfs_node_t *dir;
	size_t entry_count;
	size_t index;
	size_t produced = 0;
	size_t capacity;

	(void)cred;

	if (!node || !buffer)
		return -1;

	dir = tmpfs_from_vnode(node);
	if (!dir || !S_ISDIR(node->type))
		return -1;

	capacity = size / sizeof(dirent_t);
	if (capacity == 0) {
		if (readc)
			*readc = 0;
		return 0;
	}

	mutex_lock(&node->lock);
	index = (size_t)offset;
	while (produced < capacity) {
		if (index == 0) {
			tmpfs_fill_dirent(&buffer[produced], dir, ".", (off_t)(index + 1));
		} else if (index == 1) {
			tmpfs_node_t *parent = dir->parent ? dir->parent : dir;
			tmpfs_fill_dirent(&buffer[produced], parent, "..",
							  (off_t)(index + 1));
		} else {
			tmpfs_node_t *child = tmpfs_child_at(dir, index - 2);

			if (!child)
				break;

			tmpfs_fill_dirent(&buffer[produced], child, child->name,
							  (off_t)(index + 1));
		}

		produced++;
		index++;
	}
	mutex_unlock(&node->lock);

	entry_count = produced * sizeof(dirent_t);
	if (readc)
		*readc = entry_count;
	ktrace("getdents inode=%llu offset=%ju entries=%zu bytes=%zu",
		   (unsigned long long)dir->inode, (uintmax_t)offset, produced,
		   entry_count);
	return 0;
}

static void tmpfs_inactive(vnode_t *node)
{
	(void)node;
}

static int tmpfs_sync_node(vnode_t *node)
{
	(void)node;
	return 0;
}

void tmpfs_init(void)
{
	int rc = vfs_register(&tmpfs_vfsops, "tmpfs");

	if (rc == 0)
		klog("registered tmpfs");
	else
		klog("failed to register tmpfs");
}
