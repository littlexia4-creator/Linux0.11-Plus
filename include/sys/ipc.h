#ifndef _SYS_IPC_H
#define _SYS_IPC_H

#include <sys/types.h>

#ifndef __key_t_defined
#define __key_t_defined
typedef int key_t;
#endif

#define IPC_PRIVATE ((key_t)0)
#define IPC_CREAT  01000
#define IPC_EXCL   02000
#define IPC_NOWAIT 04000
#define IPC_RMID 0
#define IPC_SET  1
#define IPC_STAT 2

struct ipc_perm {
	key_t __key;
	uid_t uid, cuid;
	gid_t gid, cgid;
	unsigned short mode, __seq;
};

#endif
