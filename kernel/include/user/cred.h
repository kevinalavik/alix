#ifndef USER_CRED_H
#define USER_CRED_H

// thanks to https://github.com/Mathewnd/Astral

typedef struct {
	int uid, euid, suid;
	int gid, egid, sgid;
} cred_t;

#define CRED_SUPERUSER 0
#define CRED_IS_SU(cred) ((cred)->uid == CRED_SUPERUSER)
#define CRED_IS_ESU(cred) ((cred)->euid == CRED_SUPERUSER)
#define CRED_IS_SSU(cred) ((cred)->suid == CRED_SUPERUSER)

#endif // USER_CRED_H