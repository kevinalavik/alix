#include <fs/vfs.h>

#include <lib/string.h>
#define KLOG_NS "vfs"
#include <log/klog.h>

typedef struct vfs_type {
	struct vfs_type *next;
	vfsops_t *ops;
	char *name;
} vfs_type_t;

static mutex_t vfs_registry_lock = MUTEX_INIT;
static vfs_type_t *vfs_registry;

vnode_t *vfsroot;

static size_t vfs_component_length(const char *path)
{
	size_t len = 0;

	while (path[len] != '\0' && path[len] != '/')
		len++;

	return len;
}

static const char *vfs_skip_slashes(const char *path)
{
	while (*path == '/')
		path++;

	return path;
}

static bool vfs_component_equal(const char *lhs, const char *rhs, size_t len)
{
	return rhs[len] == '\0' && memcmp(lhs, rhs, len) == 0;
}

static bool vfs_string_equal(const char *lhs, const char *rhs)
{
	size_t lhs_len;
	size_t rhs_len;

	if (!lhs || !rhs)
		return false;

	lhs_len = strlen(lhs);
	rhs_len = strlen(rhs);
	return lhs_len == rhs_len && memcmp(lhs, rhs, lhs_len) == 0;
}

static int vfs_is_absolute_path(const char *path)
{
	return path && path[0] == '/';
}

static char *vfs_strdup(const char *src)
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

static vfs_type_t *vfs_find_type(const char *name)
{
	vfs_type_t *entry;

	for (entry = vfs_registry; entry; entry = entry->next) {
		if (!entry->name)
			continue;

		if (vfs_string_equal(name, entry->name))
			return entry;
	}

	return NULL;
}

static vnode_t *vfs_follow_mounts(vnode_t *node)
{
	while (node && node->mounted_vfs && node->mounted_vfs->root)
		node = node->mounted_vfs->root;

	return node;
}

advlock_t *advlock_allocate(void)
{
	advlock_t *advlock = kmalloc(sizeof(*advlock));

	if (!advlock) {
		klog("failed to allocate advisory lock");
		return NULL;
	}

	memset(advlock, 0, sizeof(*advlock));
	advlock->refcount = 1;
	ktrace("allocated advisory lock %p", advlock);
	return advlock;
}

void advlock_free(advlock_t *advlock)
{
	if (!advlock)
		return;

	kfree(advlock);
}

void vnode_hold(vnode_t *node)
{
	if (!node) {
		klog("vnode_hold called with NULL node");
		return;
	}

	__atomic_add_fetch(&node->refcount, 1, __ATOMIC_SEQ_CST);
	ktrace("hold vnode=%p refcount=%d", node, node->refcount);
}

void vnode_release(vnode_t **node)
{
	vnode_t *current;

	if (!node || !*node) {
		klog("vnode_release called with NULL handle");
		return;
	}

	current = *node;
	ktrace("release vnode=%p refcount=%d", current, current->refcount);
	if (__atomic_sub_fetch(&current->refcount, 1, __ATOMIC_SEQ_CST) == 0)
		vfs_inactive(current);

	*node = NULL;
}

void vfs_init(void)
{
	vfsroot = NULL;
	vfs_registry = NULL;
	klog("initialized VFS core");
}

int vfs_register(vfsops_t *vfsops, char *name)
{
	vfs_type_t *entry;

	if (!vfsops || !name || !*name) {
		klog("register rejected: invalid arguments vfsops=%p name=%p", vfsops,
			 name);
		return -1;
	}

	mutex_lock(&vfs_registry_lock);
	if (vfs_find_type(name)) {
		ktrace("register rejected for duplicate fs type '%s'", name);
		mutex_unlock(&vfs_registry_lock);
		return -1;
	}

	entry = kmalloc(sizeof(*entry));
	if (!entry) {
		klog("register failed for '%s': no memory for registry entry", name);
		mutex_unlock(&vfs_registry_lock);
		return -1;
	}

	entry->name = vfs_strdup(name);
	if (!entry->name) {
		klog("register failed for '%s': no memory for fs name", name);
		mutex_unlock(&vfs_registry_lock);
		kfree(entry);
		return -1;
	}

	entry->ops = vfsops;
	entry->next = vfs_registry;
	vfs_registry = entry;
	mutex_unlock(&vfs_registry_lock);
	klog("registered fs type '%s'", name);

	return 0;
}

int vfs_mount(vnode_t *backing, vnode_t *pathref, char *path, char *name,
			  void *data)
{
	vfs_type_t *type;
	vnode_t *mountpoint = pathref;
	vfs_t *mounted;
	int rc;

	if (!name) {
		klog("mount rejected: NULL filesystem name");
		return -1;
	}

	ktrace("mount request fs='%s' path='%s' pathref=%p backing=%p", name,
		   path ? path : "<root>", pathref, backing);
	mutex_lock(&vfs_registry_lock);
	type = vfs_find_type(name);
	mutex_unlock(&vfs_registry_lock);
	if (!type || !type->ops || !type->ops->mount) {
		klog("mount failed for '%s': filesystem type not registered or missing mount op",
			 name);
		return -1;
	}

	if (path) {
		rc = vfs_lookup(&mountpoint, pathref, path, NULL, 0);
		if (rc != 0) {
			klog("mount failed for '%s': lookup of '%s' returned %d", name, path,
				 rc);
			return rc;
		}
	}

	rc = type->ops->mount(&mounted, mountpoint, backing, data);
	if (rc != 0) {
		klog("mount callback for '%s' failed with %d", name, rc);
		return rc;
	}

	if (!mounted) {
		klog("mount callback for '%s' returned success with NULL vfs", name);
		return -1;
	}

	if (!mounted->root && mounted->ops && mounted->ops->root) {
		rc = mounted->ops->root(mounted, &mounted->root);
		if (rc != 0) {
			klog("root fetch for mounted '%s' failed with %d", name, rc);
			return rc;
		}
	}

	if (!mounted->root) {
		klog("mount failed for '%s': mounted filesystem has no root vnode", name);
		return -1;
	}

	mounted->covered = mountpoint;
	if (mountpoint)
		mountpoint->mounted_vfs = mounted;

	if (!mountpoint) {
		vfsroot = mounted->root;
		klog("set global VFS root to vnode=%p", vfsroot);
	}

	klog("mounted fs '%s' at %s", name, mountpoint ? "vnode" : "root");

	return 0;
}

int vfs_lookup(vnode_t **result, vnode_t *start, char *path, char *lastcomp,
			   int flags)
{
	vnode_t *current;
	const char *cursor;
	char component[256];

	if (!result || !path) {
		klog("lookup rejected: result=%p path=%p", result, path);
		return -1;
	}

	ktrace("lookup start='%p' path='%s' flags=0x%x", start, path, flags);
	current = vfs_is_absolute_path(path) ? vfsroot : start;
	current = vfs_follow_mounts(current);
	if (!current) {
		klog("lookup failed for '%s': no starting vnode", path);
		return -1;
	}

	cursor = vfs_skip_slashes(path);
	if (*cursor == '\0') {
		*result = current;
		vnode_hold(current);
		return 0;
	}

	for (;;) {
		size_t component_len;
		vnode_t *next;
		int rc;

		component_len = vfs_component_length(cursor);
		if (component_len == 0 || component_len >= sizeof(component)) {
			klog("lookup failed for '%s': invalid component length %zu", path,
				 component_len);
			return -1;
		}

		memcpy(component, cursor, component_len);
		component[component_len] = '\0';
		cursor += component_len;
		cursor = vfs_skip_slashes(cursor);

		if ((flags & VFS_LOOKUP_PARENT) && *cursor == '\0') {
			if (lastcomp)
				strlcpy(lastcomp, component, component_len + 1);
			*result = current;
			vnode_hold(current);
			ktrace("lookup parent resolved path='%s' leaf='%s' parent=%p", path,
				   component, current);
			return 0;
		}

		if (vfs_component_equal(component, ".", 1))
			continue;

		if (vfs_component_equal(component, "..", 2)) {
			klog("lookup for '%s' rejected: '..' is not implemented", path);
			return -1;
		}

		if (!current->ops || !current->ops->lookup) {
			klog("lookup failed for '%s': vnode %p has no lookup op", path,
				 current);
			return -1;
		}

		rc = current->ops->lookup(current, component, &next, NULL);
		if (rc != 0) {
			klog("lookup failed for '%s': component '%s' returned %d", path,
				 component, rc);
			return rc;
		}

		current = vfs_follow_mounts(next);
		if (!current) {
			klog("lookup failed for '%s': component '%s' resolved to NULL vnode",
				 path, component);
			return -1;
		}

		if (*cursor == '\0') {
			*result = current;
			vnode_hold(current);
			ktrace("lookup resolved path='%s' vnode=%p", path, current);
			return 0;
		}
	}
}

int vfs_open(vnode_t *ref, char *path, int flags, vnode_t **result)
{
	int rc;
	vnode_t *node;

	if (!result) {
		klog("open rejected: NULL result storage");
		return -1;
	}

	ktrace("open request ref=%p path='%s' flags=0x%x", ref,
		   path ? path : "<direct>", flags);
	if (path) {
		rc = vfs_lookup(&node, ref, path, NULL, 0);
		if (rc != 0) {
			klog("open failed for path '%s': lookup returned %d", path, rc);
			return rc;
		}
	} else {
		node = ref;
		if (!node) {
			klog("open rejected: NULL direct vnode");
			return -1;
		}
		vnode_hold(node);
	}

	if (node->ops && node->ops->open) {
		rc = node->ops->open(&node, flags, NULL);
		if (rc != 0) {
			klog("open op failed for vnode=%p flags=0x%x rc=%d", node, flags, rc);
			vnode_release(&node);
			return rc;
		}
	} else {
		ktrace("open fell back to no-op for vnode=%p", node);
	}

	*result = node;
	ktrace("open resolved vnode=%p", node);
	return 0;
}

int vfs_close(vnode_t *node, int flags)
{
	int rc = 0;

	if (!node) {
		klog("close rejected: NULL vnode");
		return -1;
	}

	ktrace("close vnode=%p flags=0x%x", node, flags);
	if (node->ops && node->ops->close)
		rc = node->ops->close(node, flags, NULL);
	else
		ktrace("close fell back to no-op for vnode=%p", node);

	vnode_release(&node);
	if (rc != 0)
		klog("close op failed rc=%d", rc);
	return rc;
}

int vfs_write(vnode_t *node, const void *buffer, size_t size,
			  uintmax_t offset, size_t *written, int flags)
{
	if (!node || !node->ops || !node->ops->write) {
		klog("write rejected: vnode=%p write-op=%p", node,
			 node && node->ops ? node->ops->write : NULL);
		return -1;
	}

	ktrace("write vnode=%p size=%zu offset=%ju flags=0x%x", node, size,
		   (uintmax_t)offset, flags);
	{
		int rc =
			node->ops->write(node, buffer, size, offset, flags, written, NULL);
		if (rc != 0)
			klog("write op failed vnode=%p rc=%d", node, rc);
		return rc;
	}
}

int vfs_read(vnode_t *node, void *buffer, size_t size, uintmax_t offset,
			 size_t *bytesread, int flags)
{
	if (!node || !node->ops || !node->ops->read) {
		klog("read rejected: vnode=%p read-op=%p", node,
			 node && node->ops ? node->ops->read : NULL);
		return -1;
	}

	ktrace("read vnode=%p size=%zu offset=%ju flags=0x%x", node, size,
		   (uintmax_t)offset, flags);
	{
		int rc =
			node->ops->read(node, buffer, size, offset, flags, bytesread, NULL);
		if (rc != 0)
			klog("read op failed vnode=%p rc=%d", node, rc);
		return rc;
	}
}

int vfs_getdents(vnode_t *node, dirent_t *buffer, size_t size,
				 uintmax_t offset, size_t *bytesread)
{
	if (!node || !node->ops || !node->ops->getdents) {
		klog("getdents rejected: vnode=%p getdents-op=%p", node,
			 node && node->ops ? node->ops->getdents : NULL);
		return -1;
	}

	ktrace("getdents vnode=%p size=%zu offset=%ju", node, size,
		   (uintmax_t)offset);
	{
		int rc =
			node->ops->getdents(node, buffer, size, offset, bytesread, NULL);
		if (rc != 0)
			klog("getdents op failed vnode=%p rc=%d", node, rc);
		return rc;
	}
}

int vfs_create(vnode_t *ref, char *path, vattr_t *attr, mode_t type,
			   vnode_t **node)
{
	char lastcomp[256];
	vnode_t *parent;
	int rc;

	if (!ref || !path || !node) {
		klog("create rejected: ref=%p path=%p node=%p", ref, path, node);
		return -1;
	}

	ktrace("create ref=%p path='%s' type=%d", ref, path, type);
	lastcomp[0] = '\0';

	rc = vfs_lookup(&parent, ref, path, lastcomp, VFS_LOOKUP_PARENT);
	if (rc != 0) {
		klog("create failed for '%s': parent lookup rc=%d", path, rc);
		return rc;
	}

	if (lastcomp[0] == '\0') {
		klog("create failed for '%s': empty final component", path);
		vnode_release(&parent);
		return -1;
	}

	if (!parent->ops || !parent->ops->create) {
		klog("create failed for '%s': parent vnode=%p has no create op", path,
			 parent);
		vnode_release(&parent);
		return -1;
	}

	rc = parent->ops->create(parent, lastcomp, attr, type, node, NULL);
	vnode_release(&parent);
	if (rc == 0)
		ktrace("create produced vnode=%p for path='%s'", *node, path);
	else
		klog("create failed for '%s': create op rc=%d", path, rc);
	return rc;
}

void vfs_inactive(vnode_t *node)
{
	if (!node) {
		klog("inactive called with NULL vnode");
		return;
	}

	ktrace("inactive vnode=%p", node);

	if (node->ops && node->ops->inactive)
		node->ops->inactive(node);

	if (node->advlock) {
		ADVLOCK_UNREF(node->advlock);
		node->advlock = NULL;
	}
}
