#ifndef FS_VFS_H
#define FS_VFS_H

#include <stddef.h>
#include <stdint.h>

#include <lib/mutex.h>
#include <mm/mm.h>
#include <user/cred.h>
#include <user/posix.h>

typedef struct vnode vnode_t;
typedef struct vfs vfs_t;
typedef struct vops vops_t;
typedef struct vfsops vfsops_t;
typedef struct advlock advlock_t;

typedef struct {
	mode_t type;
	mode_t mode;
	uid_t uid;
	gid_t gid;
	int fsid;
	ino_t inode;
	int nlinks;
	size_t size;
	size_t block_size;
	timespec_t atime;
	timespec_t mtime;
	timespec_t ctime;
	size_t blocksused;
} vattr_t;

typedef struct {
	size_t io_size;
	size_t block_size;
	size_t block_count;
	size_t free_blocks;
	size_t free_blocks_unprivileged;
	size_t inode_count;
	size_t free_inode_count;
	size_t free_inode_count_unprivileged;
	unsigned long fsid;
	unsigned long flags;
	size_t max_name_size;
} fsattr_t;

struct vops {
	int (*open)(vnode_t **node, int flags, cred_t *cred);
	int (*close)(vnode_t *node, int flags, cred_t *cred);
	int (*read)(vnode_t *node, void *buffer, size_t size, uintmax_t offset,
				int flags, size_t *readc, cred_t *cred);
	int (*write)(vnode_t *node, const void *buffer, size_t size,
				 uintmax_t offset, int flags, size_t *writec, cred_t *cred);
	int (*lookup)(vnode_t *node, char *name, vnode_t **result, cred_t *cred);
	int (*create)(vnode_t *parent, char *name, vattr_t *attr, mode_t type,
				  vnode_t **result, cred_t *cred);
	int (*getdents)(vnode_t *node, dirent_t *buffer, size_t size,
					uintmax_t offset, size_t *readc, cred_t *cred);
	void (*inactive)(vnode_t *node);
	int (*sync)(vnode_t *node);
};

struct vfsops {
	int (*mount)(vfs_t **vfs, vnode_t *mountpoint, vnode_t *backing, void *data);
	int (*unmount)(vfs_t *vfs);
	int (*sync)(vfs_t *vfs);
	int (*root)(vfs_t *vfs, vnode_t **root);
	int (*statfs)(vfs_t *vfs, fsattr_t *fsattr);
};

struct vfs {
	vfs_t *next;
	vfsops_t *ops;
	vnode_t *covered;
	vnode_t *root;
	int flags;
};

#define ADVLOCK_SHARED 1
#define ADVLOCK_EXCLUSIVE 2
#define ADVLOCK_NON_BLOCKING 4
#define ADVLOCK_UNLOCK 8

struct advlock {
	int type;
	int refcount;
	int shared_count;
};

#define ADVLOCK_REF(lock) \
	__atomic_fetch_add(&(lock)->refcount, 1, __ATOMIC_SEQ_CST)

#define ADVLOCK_UNREF(lock)                                              \
	do {                                                                 \
		if (__atomic_sub_fetch(&(lock)->refcount, 1, __ATOMIC_SEQ_CST) == \
			0)                                                            \
			advlock_free(lock);                                           \
	} while (0)

struct vnode {
	vops_t *ops;
	mutex_t lock;
	mutex_t size_lock;
	int refcount;
	int flags;
	mode_t type;
	vfs_t *vfs;
	vfs_t *mounted_vfs;
	void *private_data;
	page_t *pages;
	mutex_t adv_mutex;
	advlock_t *advlock;
};

static inline void vfs_struct_init(vfs_t *vfs, vfsops_t *ops, int flags)
{
	if (!vfs)
		return;

	vfs->next = NULL;
	vfs->ops = ops;
	vfs->covered = NULL;
	vfs->root = NULL;
	vfs->flags = flags;
}

static inline void vnode_init(vnode_t *node, vops_t *ops, int flags,
							  mode_t type, vfs_t *vfs)
{
	if (!node)
		return;

	node->ops = ops;
	mutex_init(&node->lock);
	mutex_init(&node->size_lock);
	node->refcount = 1;
	node->flags = flags;
	node->type = type;
	node->vfs = vfs;
	node->mounted_vfs = NULL;
	node->private_data = NULL;
	node->pages = NULL;
	mutex_init(&node->adv_mutex);
	node->advlock = NULL;
}

#define VFS_LOOKUP_PARENT 0x20000000
#define VFS_LOOKUP_NOLINK 0x40000000
#define VFS_LOOKUP_INTERNAL 0x80000000

extern vnode_t *vfsroot;

advlock_t *advlock_allocate(void);
void advlock_free(advlock_t *advlock);

void vnode_hold(vnode_t *node);
void vnode_release(vnode_t **node);

void vfs_init(void);
int vfs_mount(vnode_t *backing, vnode_t *pathref, char *path, char *name,
			  void *data);
int vfs_register(vfsops_t *vfsops, char *name);

int vfs_open(vnode_t *ref, char *path, int flags, vnode_t **result);
int vfs_close(vnode_t *node, int flags);
int vfs_write(vnode_t *node, const void *buffer, size_t size,
			  uintmax_t offset, size_t *written, int flags);
int vfs_read(vnode_t *node, void *buffer, size_t size, uintmax_t offset,
			 size_t *bytesread, int flags);
int vfs_getdents(vnode_t *node, dirent_t *buffer, size_t size,
				 uintmax_t offset, size_t *bytesread);
int vfs_create(vnode_t *ref, char *path, vattr_t *attr, mode_t type,
			   vnode_t **node);
void vfs_inactive(vnode_t *node);

int vfs_lookup(vnode_t **result, vnode_t *start, char *path, char *lastcomp,
			   int flags);

#endif // FS_VFS_H
