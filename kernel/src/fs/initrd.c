#include <fs/initrd.h>
#include <fs/vfs.h>
#include <lib/string.h>
#define KLOG_NS "initrd"
#include <log/klog.h>

typedef struct cpio_newc_header {
	char c_magic[6];
	char c_ino[8];
	char c_mode[8];
	char c_uid[8];
	char c_gid[8];
	char c_nlink[8];
	char c_mtime[8];
	char c_filesize[8];
	char c_devmajor[8];
	char c_devminor[8];
	char c_rdevmajor[8];
	char c_rdevminor[8];
	char c_namesize[8];
	char c_check[8];
} cpio_newc_header_t;

static uint32_t initrd_parse_hex(const char *hex, size_t len, bool *ok)
{
	uint32_t value = 0;

	*ok = false;
	if (!hex)
		return 0;

	for (size_t i = 0; i < len; i++) {
		uint32_t digit;
		char c = hex[i];

		if (c >= '0' && c <= '9')
			digit = (uint32_t)(c - '0');
		else if (c >= 'a' && c <= 'f')
			digit = (uint32_t)(c - 'a' + 10);
		else if (c >= 'A' && c <= 'F')
			digit = (uint32_t)(c - 'A' + 10);
		else
			return 0;

		value = (value << 4) | digit;
	}

	*ok = true;
	return value;
}

static size_t initrd_align4(size_t value)
{
	return (value + 3) & ~(size_t)3;
}

static bool initrd_string_equal(const char *lhs, const char *rhs)
{
	size_t lhs_len;
	size_t rhs_len;

	if (!lhs || !rhs)
		return false;

	lhs_len = strlen(lhs);
	rhs_len = strlen(rhs);
	return lhs_len == rhs_len && memcmp(lhs, rhs, lhs_len) == 0;
}

static void initrd_normalize_path(const char *name, char *path,
								  size_t path_size)
{
	size_t src = 0;
	size_t dst = 0;

	if (path_size == 0)
		return;

	while (name && name[src] == '.' &&
		   (name[src + 1] == '/' || name[src + 1] == '\0')) {
		if (name[src + 1] == '\0') {
			path[0] = '\0';
			return;
		}
		src += 2;
	}

	if (dst + 1 < path_size)
		path[dst++] = '/';

	while (name && name[src] != '\0' && dst + 1 < path_size)
		path[dst++] = name[src++];

	path[dst] = '\0';
}

static int initrd_ensure_dir(const char *path, mode_t mode)
{
	vnode_t *node;
	vattr_t attr;

	if (!path || !*path || initrd_string_equal(path, "/"))
		return 0;

	if (vfs_lookup(&node, vfsroot, (char *)path, NULL, 0) == 0) {
		vnode_release(&node);
		return 0;
	}

	memset(&attr, 0, sizeof(attr));
	attr.mode = S_IFDIR | (mode & ACCESSPERMS);
	if (vfs_create(vfsroot, (char *)path, &attr, S_IFDIR, &node) != 0) {
		ktrace("failed to create directory '%s' from initrd", path);
		return -1;
	}

	ktrace("created initrd directory '%s'", path);
	vnode_release(&node);
	return 0;
}

static int initrd_ensure_parent_dirs(const char *path)
{
	char partial[PATH_MAX];
	size_t len;

	if (!path)
		return -1;

	len = strlen(path);
	if (len >= sizeof(partial))
		return -1;

	memset(partial, 0, sizeof(partial));
	for (size_t i = 1; i < len; i++) {
		if (path[i] != '/')
			continue;

		memcpy(partial, path, i);
		partial[i] = '\0';
		if (initrd_ensure_dir(partial, 0755) != 0)
			return -1;
	}

	return 0;
}

static int initrd_unpack_dir(const char *path, mode_t mode)
{
	if (initrd_ensure_parent_dirs(path) != 0)
		return -1;

	return initrd_ensure_dir(path, mode);
}

static int initrd_unpack_file(const char *path, mode_t mode, const void *data,
							  size_t size)
{
	vnode_t *node;
	vattr_t attr;
	size_t written = 0;

	if (initrd_ensure_parent_dirs(path) != 0)
		return -1;

	memset(&attr, 0, sizeof(attr));
	attr.mode = S_IFREG | (mode & ACCESSPERMS);
	if (vfs_create(vfsroot, (char *)path, &attr, S_IFREG, &node) != 0) {
		ktrace("failed to create file '%s' from initrd", path);
		return -1;
	}

	if (size != 0 &&
		vfs_write(node, data, size, 0, &written, O_WRONLY | O_TRUNC) != 0) {
		ktrace("failed to write file '%s' from initrd", path);
		vnode_release(&node);
		return -1;
	}

	ktrace("unpacked initrd file '%s' (%zu bytes)", path, written);
	vnode_release(&node);
	return 0;
}

int initrd_unpack(const void *buffer, size_t length)
{
	const uint8_t *cursor;
	const uint8_t *end;

	if (!buffer || length == 0) {
		klog("initrd unpack rejected: buffer=%p length=%zu", buffer, length);
		return -1;
	}

	ktrace("unpacking initrd buffer=%p length=%zu", buffer, length);
	cursor = (const uint8_t *)buffer;
	end = cursor + length;

	while (cursor + sizeof(cpio_newc_header_t) <= end) {
		const cpio_newc_header_t *header =
			(const cpio_newc_header_t *)(const void *)cursor;
		bool ok;
		uint32_t mode;
		uint32_t name_size;
		uint32_t file_size;
		const char *name;
		const uint8_t *data;
		char path[PATH_MAX];

		if (memcmp(header->c_magic, "070701", 6) != 0) {
			klog("unsupported cpio magic in initrd");
			return -1;
		}

		mode = initrd_parse_hex(header->c_mode, sizeof(header->c_mode), &ok);
		if (!ok)
			return -1;
		name_size = initrd_parse_hex(header->c_namesize,
									 sizeof(header->c_namesize), &ok);
		if (!ok || name_size == 0)
			return -1;
		file_size = initrd_parse_hex(header->c_filesize,
									 sizeof(header->c_filesize), &ok);
		if (!ok)
			return -1;

		name = (const char *)(cursor + sizeof(*header));
		if ((const uint8_t *)name + name_size > end) {
			klog("cpio entry overruns module while reading name");
			return -1;
		}

		data = cursor + initrd_align4(sizeof(*header) + name_size);
		if (data + file_size > end) {
			klog("cpio entry '%s' overruns module while reading data", name);
			return -1;
		}

		if (initrd_string_equal(name, "TRAILER!!!"))
			break;

		initrd_normalize_path(name, path, sizeof(path));
		if (path[0] == '\0') {
			cursor = data + initrd_align4(file_size);
			continue;
		}

		if (S_ISDIR(mode)) {
			if (initrd_unpack_dir(path, mode) != 0)
				return -1;
		} else if (S_ISREG(mode)) {
			if (initrd_unpack_file(path, mode, data, file_size) != 0)
				return -1;
		} else {
			klog("skipping unsupported initrd entry '%s' mode=0%o", path,
				 (unsigned int)mode);
		}

		cursor = data + initrd_align4(file_size);
	}

	klog("unpacked initrd, total size %zu bytes",
		 (size_t)(cursor - (const uint8_t *)buffer));
	return 0;
}
