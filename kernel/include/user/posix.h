#ifndef USER_POSIX_H
#define USER_POSIX_H

#include <stdint.h>

typedef int pid_t;
typedef int tid_t;
typedef int gid_t;
typedef int uid_t;
typedef unsigned int mode_t;
typedef uint64_t ino_t;
typedef int64_t off_t;

#define NAME_MAX 255
#define PATH_MAX 4096

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

#define O_RDONLY 0x0000
#define O_WRONLY 0x0001
#define O_RDWR 0x0002
#define O_ACCMODE 0x0003
#define O_CREAT 0x0100
#define O_EXCL 0x0200
#define O_NOCTTY 0x0400
#define O_TRUNC 0x0800
#define O_APPEND 0x1000
#define O_NONBLOCK 0x2000
#define O_DIRECTORY 0x4000
#define O_NOFOLLOW 0x8000
#define O_CLOEXEC 0x10000

#define S_IFMT 00170000U
#define S_IFSOCK 0140000U
#define S_IFLNK 0120000U
#define S_IFREG 0100000U
#define S_IFBLK 0060000U
#define S_IFDIR 0040000U
#define S_IFCHR 0020000U
#define S_IFIFO 0010000U

#define S_ISUID 0004000U
#define S_ISGID 0002000U
#define S_ISVTX 0001000U

#define S_IRUSR 0000400U
#define S_IWUSR 0000200U
#define S_IXUSR 0000100U
#define S_IRWXU (S_IRUSR | S_IWUSR | S_IXUSR)

#define S_IRGRP 0000040U
#define S_IWGRP 0000020U
#define S_IXGRP 0000010U
#define S_IRWXG (S_IRGRP | S_IWGRP | S_IXGRP)

#define S_IROTH 0000004U
#define S_IWOTH 0000002U
#define S_IXOTH 0000001U
#define S_IRWXO (S_IROTH | S_IWOTH | S_IXOTH)

#define ACCESSPERMS (S_IRWXU | S_IRWXG | S_IRWXO)
#define ALLPERMS (S_ISUID | S_ISGID | S_ISVTX | ACCESSPERMS)
#define DEFFILEMODE 0666

#define S_ISREG(mode) (((mode)&S_IFMT) == S_IFREG)
#define S_ISDIR(mode) (((mode)&S_IFMT) == S_IFDIR)
#define S_ISCHR(mode) (((mode)&S_IFMT) == S_IFCHR)
#define S_ISBLK(mode) (((mode)&S_IFMT) == S_IFBLK)
#define S_ISFIFO(mode) (((mode)&S_IFMT) == S_IFIFO)
#define S_ISLNK(mode) (((mode)&S_IFMT) == S_IFLNK)
#define S_ISSOCK(mode) (((mode)&S_IFMT) == S_IFSOCK)

#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12

typedef long time_t;
typedef long suseconds_t;

typedef struct {
	time_t s;
	time_t ns;
} timespec_t;

typedef struct {
	time_t s;
	suseconds_t us;
} timeval_t;

typedef struct dirent {
	ino_t d_ino;
	off_t d_off;
	uint16_t d_reclen;
	uint8_t d_type;
	char d_name[NAME_MAX + 1];
} dirent_t;

#endif // USER_POSIX_H
