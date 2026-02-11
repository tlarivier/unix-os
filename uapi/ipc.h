#ifndef UAPI_IPC_H
#define UAPI_IPC_H

#include <../uapi/types.h>

#ifndef _GID_T_DEFINED
#define _GID_T_DEFINED
typedef uint32_t gid_t;
#endif

#ifndef _MODE_T_DEFINED
#define _MODE_T_DEFINED  
typedef uint32_t mode_t;
#endif

#define PIPE_BUF        4096    /* Atomic write size for pipes */
#define PIPE_MAX_SIZE   65536   /* Maximum pipe buffer size */

#define O_CLOEXEC       0x80000  /* Close on exec */
#ifndef O_NONBLOCK
#define O_NONBLOCK      0x1000   /* Non-blocking I/O - consistent with UAPI */
#endif

#define MSGMAX          8192    
#define MSGMNB          16384   
#define MSGMNI          16      

#define SHMMAX          (32 * 1024 * 1024)  
#define SHMMIN          1                   
#define SHMMNI          4096                
#define SHMALL          (8 * 1024)          

#define SEMMAX          32767   
#define SEMMNI          128     
#define SEMMSL          250     

#define IPC_PRIVATE     0       
#define IPC_CREAT       01000   
#define IPC_EXCL        02000   
#define IPC_NOWAIT      04000   

#define IPC_RMID        0       
#define IPC_SET         1       
#define IPC_STAT        2       
#define IPC_INFO        3       

struct ipc_perm {
    uid_t   uid;        
    gid_t   gid;        
    uid_t   cuid;       
    gid_t   cgid;       
    mode_t  mode;       
    uint32_t seq;       
    uint32_t key;       
};

#endif 
