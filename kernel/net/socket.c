#include <kernel/process.h>
#include <kernel/memory.h>
#include <kernel/errno.h>
#include <kernel/spinlock.h>
#include <kernel/uaccess.h>
#include <stdint.h>
#include <stddef.h>

/* Not implemented */

#define AF_UNSPEC   0
#define AF_UNIX     1
#define AF_LOCAL    AF_UNIX
#define AF_INET     2   

#define SOCK_STREAM     1   
#define SOCK_DGRAM      2  
#define SOCK_RAW        3

#define SS_FREE         0
#define SS_UNCONNECTED  1
#define SS_CONNECTING   2
#define SS_CONNECTED    3
#define SS_LISTENING    4

#define SOL_SOCKET      1
#define SO_REUSEADDR    2
#define SO_ERROR        4
#define SO_BROADCAST    6
#define SO_SNDBUF       7
#define SO_RCVBUF       8

#define SHUT_RD     0
#define SHUT_WR     1
#define SHUT_RDWR   2

#define UNIX_PATH_MAX   108

typedef struct sockaddr {
    uint16_t sa_family;
    char sa_data[14];
} sockaddr_t;

typedef struct sockaddr_un {
    uint16_t sun_family;
    char sun_path[UNIX_PATH_MAX];
} sockaddr_un_t;

#define SOCKET_BUF_SIZE 4096

typedef struct sock_buf {
    char data[SOCKET_BUF_SIZE];
    size_t head;
    size_t tail;
    size_t count;
} sock_buf_t;

typedef struct socket {
    int             fd;         
    int             domain;     
    int             type;       
    int             protocol;
    int             state;
    
    char            bound_path[UNIX_PATH_MAX];
    
    struct socket*  peer;      
    struct socket*  accept_queue[8];  
    int             accept_count;
    int             backlog;
    
    sock_buf_t      recv_buf;
    sock_buf_t      send_buf;
    
    pid_t           owner_pid;
    
    int             nonblock;
    int             in_use;
    
    spinlock_t      lock;
} socket_t;

#define MAX_SOCKETS 64
static socket_t socket_table[MAX_SOCKETS];
static spinlock_t socket_lock;
static int socket_initialized = 0;

void socket_init(void) {
    if (socket_initialized) return;
    
    spinlock_init(&socket_lock, "socket");
    for (int i = 0; i < MAX_SOCKETS; i++) {
        socket_table[i].in_use = 0;
        socket_table[i].fd = -1;
        spinlock_init(&socket_table[i].lock, "sock");
    }
    socket_initialized = 1;
}

static socket_t* socket_alloc(void) {
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (!socket_table[i].in_use) {
            socket_table[i].in_use         = 1;
            socket_table[i].state          = SS_UNCONNECTED;
            socket_table[i].peer           = NULL;
            socket_table[i].accept_count   = 0;
            socket_table[i].backlog        = 0;
            socket_table[i].nonblock       = 0;
            socket_table[i].recv_buf.head  = 0;
            socket_table[i].recv_buf.tail  = 0;
            socket_table[i].recv_buf.count = 0;
            socket_table[i].send_buf.head  = 0;
            socket_table[i].send_buf.tail  = 0;
            socket_table[i].send_buf.count = 0;
            socket_table[i].bound_path[0]  = '\0';
            return &socket_table[i];
        }
    }
    return NULL;
}

static socket_t* socket_find_by_path(const char* path) {
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (socket_table[i].in_use && socket_table[i].bound_path[0] != '\0') {
            int match = 1;
            for (int j = 0; j < UNIX_PATH_MAX && match; j++) {
                if (socket_table[i].bound_path[j] != path[j]) match = 0;
                if (path[j] == '\0') break;
            }
            if (match) return &socket_table[i];
        }
    }
    return NULL;
}

static socket_t* socket_get(int fd) {
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (socket_table[i].in_use && socket_table[i].fd == fd) {
            return &socket_table[i];
        }
    }
    return NULL;
}

int32_t sys_socket(uint32_t domain, uint32_t type, uint32_t protocol, uint32_t u4, uint32_t u5) {
    (void)u4; (void)u5;
    
    if (!socket_initialized) socket_init();
    
    if (domain != AF_UNIX && domain != AF_LOCAL) {
        return -EAFNOSUPPORT;
    }
    if (type != SOCK_STREAM && type != SOCK_DGRAM) {
        return -ESOCKTNOSUPPORT;
    }
    
    process_t* proc = get_current_process();
    if (!proc) return -ESRCH;
    
    spin_lock(&socket_lock);
    
    socket_t* sock = socket_alloc();
    if (!sock) {
        spin_unlock(&socket_lock);
        return -ENFILE;
    }
    
    int fd = -1;
    for (int i = 3; i < MAX_OPEN_FILES_CONST; i++) {
        if (proc->fd_table[i].node_idx == 0 && proc->fd_table[i].refcount == 0) {
            fd = i;
            break;
        }
    }
    
    if (fd < 0) {
        sock->in_use = 0;
        spin_unlock(&socket_lock);
        return -EMFILE;
    }
    
    sock->fd        = fd;
    sock->domain    = (int)domain;
    sock->type      = (int)type;
    sock->protocol  = (int)protocol;
    sock->owner_pid = proc->pid;
    
    proc->fd_table[fd].node_idx = 0xFFFFFF00 | (uint32_t)(sock - socket_table);
    proc->fd_table[fd].refcount = 1;
    
    spin_unlock(&socket_lock);
    
    return fd;
}

int32_t sys_bind(uint32_t sockfd, uint32_t addr_ptr, uint32_t addrlen, uint32_t u4, uint32_t u5) {
    (void)u4; (void)u5;
    
    if (!socket_initialized) return -EINVAL;
    if (addrlen < sizeof(uint16_t)) return -EINVAL;
    
    socket_t* sock = socket_get((int)sockfd);
    if (!sock) return -ENOTSOCK;
    
    sockaddr_un_t addr;
    if (copy_from_user(&addr, (void*)addr_ptr, 
            addrlen < sizeof(addr) ? addrlen : sizeof(addr)) < 0) {
        return -EFAULT;
    }
    
    if (addr.sun_family != AF_UNIX && addr.sun_family != AF_LOCAL) {
        return -EAFNOSUPPORT;
    }
    
    spin_lock(&sock->lock);
    
    if (socket_find_by_path(addr.sun_path)) {
        spin_unlock(&sock->lock);
        return -EADDRINUSE;
    }
    
    for (int i = 0; i < UNIX_PATH_MAX - 1 && addr.sun_path[i]; i++) {
        sock->bound_path[i] = addr.sun_path[i];
        sock->bound_path[i+1] = '\0';
    }
    
    spin_unlock(&sock->lock);
    return 0;
}

int32_t sys_listen(uint32_t sockfd, uint32_t backlog, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u3; (void)u4; (void)u5;
    
    if (!socket_initialized) return -EINVAL;
    
    socket_t* sock = socket_get((int)sockfd);
    if (!sock) return -ENOTSOCK;
    
    if (sock->type != SOCK_STREAM) {
        return -EOPNOTSUPP;
    }
    
    spin_lock(&sock->lock);
    
    sock->state = SS_LISTENING;
    sock->backlog = (backlog > 8) ? 8 : (int)backlog;
    
    spin_unlock(&sock->lock);
    return 0;
}

int32_t sys_accept(uint32_t sockfd, uint32_t addr_ptr, uint32_t addrlen_ptr, uint32_t u4, uint32_t u5) {
    (void)addr_ptr; (void)addrlen_ptr; (void)u4; (void)u5;
    
    if (!socket_initialized) return -EINVAL;
    
    socket_t* sock = socket_get((int)sockfd);
    if (!sock) return -ENOTSOCK;
    
    if (sock->state != SS_LISTENING) {
        return -EINVAL;
    }
    
    process_t* proc = get_current_process();
    if (!proc) return -ESRCH;
    
    spin_lock(&sock->lock);
    
    while (sock->accept_count == 0) {
        if (sock->nonblock) {
            spin_unlock(&sock->lock);
            return -EAGAIN;
        }
        spin_unlock(&sock->lock);
        extern void yield(void);
        yield();
        spin_lock(&sock->lock);
    }
    
    socket_t* client = sock->accept_queue[0];
    
    for (int i = 0; i < sock->accept_count - 1; i++) {
        sock->accept_queue[i] = sock->accept_queue[i+1];
    }
    sock->accept_count--;
    
    spin_unlock(&sock->lock);
    
    spin_lock(&socket_lock);
    
    socket_t* newsock = socket_alloc();
    if (!newsock) {
        spin_unlock(&socket_lock);
        return -ENFILE;
    }
    
    int newfd = -1;
    for (int i = 3; i < MAX_OPEN_FILES_CONST; i++) {
        if (proc->fd_table[i].node_idx == 0 && proc->fd_table[i].refcount == 0) {
            newfd = i;
            break;
        }
    }
    
    if (newfd < 0) {
        newsock->in_use = 0;
        spin_unlock(&socket_lock);
        return -EMFILE;
    }
    
    newsock->fd        = newfd;
    newsock->domain    = sock->domain;
    newsock->type      = sock->type;
    newsock->protocol  = sock->protocol;
    newsock->owner_pid = proc->pid;
    newsock->state     = SS_CONNECTED;
    newsock->peer      = client;
    client->peer       = newsock;
    client->state      = SS_CONNECTED;
    
    proc->fd_table[newfd].node_idx = 0xFFFFFF00 | (uint32_t)(newsock - socket_table);
    proc->fd_table[newfd].refcount = 1;
    
    spin_unlock(&socket_lock);
    
    return newfd;
}

int32_t sys_connect(uint32_t sockfd, uint32_t addr_ptr, uint32_t addrlen, uint32_t u4, uint32_t u5) {
    (void)u4; (void)u5;
    
    if (!socket_initialized) return -EINVAL;
    if (addrlen < sizeof(uint16_t)) return -EINVAL;
    
    socket_t* sock = socket_get((int)sockfd);
    if (!sock) return -ENOTSOCK;
    
    sockaddr_un_t addr;
    if (copy_from_user(&addr, (void*)addr_ptr,
            addrlen < sizeof(addr) ? addrlen : sizeof(addr)) < 0) {
        return -EFAULT;
    }
    
    if (addr.sun_family != AF_UNIX) {
        return -EAFNOSUPPORT;
    }
    
    spin_lock(&socket_lock);
    
    socket_t* server = socket_find_by_path(addr.sun_path);
    if (!server || server->state != SS_LISTENING) {
        spin_unlock(&socket_lock);
        return -ECONNREFUSED;
    }
    
    if (server->accept_count >= server->backlog) {
        spin_unlock(&socket_lock);
        return -ECONNREFUSED;
    }
    
    sock->state = SS_CONNECTING;
    server->accept_queue[server->accept_count++] = sock;
    
    spin_unlock(&socket_lock);
    
    while (sock->state == SS_CONNECTING) {
        extern void yield(void);
        yield();
    }
    
    return 0;
}

int32_t sys_send(uint32_t sockfd, uint32_t buf_ptr, uint32_t len, uint32_t flags, uint32_t u5) {
    (void)flags; (void)u5;
    
    if (!socket_initialized) return -EINVAL;
    
    socket_t* sock = socket_get((int)sockfd);
    if (!sock) return -ENOTSOCK;
    
    if (sock->state != SS_CONNECTED || !sock->peer) {
        return -ENOTCONN;
    }
    
    socket_t* peer = sock->peer;
    
    spin_lock(&peer->lock);
    
    size_t written = 0;
    char kbuf[256];
    
    while (written < len) {
        size_t chunk = (len - written > 256) ? 256 : (len - written);
        
        if (copy_from_user(kbuf, (void*)(buf_ptr + written), chunk) < 0) {
            spin_unlock(&peer->lock);
            return written > 0 ? (int32_t)written : -EFAULT;
        }
        
        for (size_t i = 0; i < chunk && peer->recv_buf.count < SOCKET_BUF_SIZE; i++) {
            peer->recv_buf.data[peer->recv_buf.tail] = kbuf[i];
            peer->recv_buf.tail = (peer->recv_buf.tail + 1) % SOCKET_BUF_SIZE;
            peer->recv_buf.count++;
            written++;
        }
        
        if (peer->recv_buf.count >= SOCKET_BUF_SIZE) break;
    }
    
    spin_unlock(&peer->lock);
    
    return (int32_t)written;
}

int32_t sys_recv(uint32_t sockfd, uint32_t buf_ptr, uint32_t len, uint32_t flags, uint32_t u5) {
    (void)flags; (void)u5;
    
    if (!socket_initialized) return -EINVAL;
    
    socket_t* sock = socket_get((int)sockfd);
    if (!sock) return -ENOTSOCK;
    
    if (sock->state != SS_CONNECTED) {
        return -ENOTCONN;
    }
    
    spin_lock(&sock->lock);
    
    while (sock->recv_buf.count == 0) {
        if (!sock->peer) {
            spin_unlock(&sock->lock);
            return 0;  /* EOF */
        }
        if (sock->nonblock) {
            spin_unlock(&sock->lock);
            return -EAGAIN;
        }
        spin_unlock(&sock->lock);
        extern void yield(void);
        yield();
        spin_lock(&sock->lock);
    }
    
    size_t to_read = (len < sock->recv_buf.count) ? len : sock->recv_buf.count;
    char kbuf[256];
    size_t total_read = 0;
    
    while (total_read < to_read) {
        size_t chunk = (to_read - total_read > 256) ? 256 : (to_read - total_read);
        
        for (size_t i = 0; i < chunk; i++) {
            kbuf[i] = sock->recv_buf.data[sock->recv_buf.head];
            sock->recv_buf.head = (sock->recv_buf.head + 1) % SOCKET_BUF_SIZE;
            sock->recv_buf.count--;
        }
        
        if (copy_to_user((void*)(buf_ptr + total_read), kbuf, chunk) < 0) {
            spin_unlock(&sock->lock);
            return total_read > 0 ? (int32_t)total_read : -EFAULT;
        }
        
        total_read += chunk;
    }
    
    spin_unlock(&sock->lock);
    
    return (int32_t)total_read;
}

int32_t sys_shutdown(uint32_t sockfd, uint32_t how, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)how; (void)u3; (void)u4; (void)u5;
    
    if (!socket_initialized) return -EINVAL;
    
    socket_t* sock = socket_get((int)sockfd);
    if (!sock) return -ENOTSOCK;
    
    spin_lock(&sock->lock);
    
    if (sock->peer) {
        sock->peer->peer = NULL;
    }
    sock->peer = NULL;
    sock->state = SS_UNCONNECTED;
    
    spin_unlock(&sock->lock);
    
    return 0;
}

int socket_close(int fd) {
    if (!socket_initialized) return -EINVAL;
    
    socket_t* sock = socket_get(fd);
    if (!sock) return -ENOTSOCK;
    
    spin_lock(&sock->lock);
    
    if (sock->peer) {
        socket_t* peer = sock->peer;
        peer->peer = NULL;
    }
    
    sock->in_use = 0;
    sock->fd     = -1;
    sock->peer   = NULL;
    sock->state  = SS_FREE;
    sock->bound_path[0] = '\0';
    
    spin_unlock(&sock->lock);
    
    return 0;
}
