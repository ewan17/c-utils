#include <arpa/inet.h>
#include <sys/un.h>
#include <netinet/tcp.h>
#include <event2/event.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <regex.h>
#include <assert.h>

#include "utilities.h"

/**
 * @todo    currently broken
*/
int validate_filename(const char *filename) {
    regex_t regex;
    int regExp;

    /**
     * @note    this does not work "^[A-Za-z._-]+$"
     * @todo    fix this later
    */
    regExp = regcomp(&regex, "", 0);
    if(regExp != 0) {
        return -1;
    }

    regExp = regexec(&regex, filename, 0, NULL, 0);
    if(filename == NULL || regExp == REG_NOMATCH) {
        regfree(&regex);
        return -1;
    }

    regfree(&regex);

    return 0;
}

/**
 * @note    this function needs some work on the return values
 */
void internal_filename_from_path(const char *path, char **filename) {
    char temp[MAX_DIR_SIZE];
    size_t len;
    
    len = strlen(path);
    if(len > MAX_DIR_SIZE - 1 || len < 3) {
        goto error;
    }
    memcpy(&temp[0], path, len);

    size_t index = len - 1;
    if(temp[index] == '/') {
        index--;
    }

    size_t filenameLen = 0;
    while(temp[index] != '/' && index > 0) {
        filenameLen++;
        index--;
    }
    index++;

    if (index == 0 && temp[index] != '/') {
        goto error;
    }

    *filename = (char *) malloc(filenameLen * sizeof(char));
    if(*filename == NULL) {
        goto error;
    }

    memcpy(*filename, &temp[index], filenameLen);
    (*filename)[filenameLen] = '\0';
    return;

error:
    *filename = NULL;
}

int trash_mkdir(const char *path, size_t len, mode_t mode) {
    char holder[MAX_DIR_SIZE];
    int rc;

    if(len > MAX_DIR_SIZE) {
        return -1;
    }

    memcpy(holder, path, len);
    holder[len] = '\0';

    for (size_t i = 0; i < len; i++)
    {
        if(holder[i] == '/' && i != 0) {
            holder[i] = '\0';

            rc = mkdir(holder, mode);
            if(rc != 0 && errno != EEXIST) {
                return -1;
            }

            holder[i] = '/';
        }
    }
    return 0;
}

int trash_no_file_dir(const char *filepath) {
    int rc;
    
    size_t len = strlen(filepath);
    rc = trash_mkdir(filepath, len, 0755);
    return rc;
}

int sock_block(int fd) {
    int flags;
    flags = fcntl(fd, F_GETFL);
    if(flags == -1) {
        return -1;
    }

    flags &= ~O_NONBLOCK;
    if(fcntl(fd, F_SETFL, flags) == -1) {
        return -1;
    }

    return 0;
}

bool sock_buff_size(int sock, int send_buffy_size, int recv_buffy_size) {
    if(setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &send_buffy_size, sizeof(send_buffy_size)) != 0) {
        return false;
    }

    if(setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &recv_buffy_size, sizeof(recv_buffy_size)) != 0) {
        return false;
    }
    return true;
}

bool keep_sock_alive(int sock, int alive, int idle, int interval, int maxpkt) {
#ifdef SO_KEEPALIVE    
    if(setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &alive, sizeof(alive)) != 0) {
        return false;
    }
#endif
#ifdef TCP_KEEPIDLE    
    if(setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle)) != 0) {
        return false;
    }
#endif
#ifdef TCP_KEEPINTVL    
    if(setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval)) != 0) {
        return false;
    }
#endif
#ifdef TCP_KEEPCNT    
    if(setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &maxpkt, sizeof(maxpkt)) != 0) {
        return false;
    }
#endif
    return true;
}

int opt_sock(int sock, bool isUnixSock) {
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1) {
        return -1;
    }

    flags |= O_NONBLOCK;
    if (fcntl(sock, F_SETFL, flags) == -1) {
        return -1;
    }

    if(!isUnixSock) {
        if(!keep_sock_alive(sock, KEEP_ALIVE, KEEP_IDLE, KEEP_INTERVAL, KEEP_COUNT)) {
            return -1;
        }
    }
    return 0;
}

bool is_unix_socket(struct TrashAddr *addr) {
    return (addr->addr.ss_family == AF_UNIX);
}

/**
 * @todo    fix this. what if the socket path is absolute? 
*/
void get_sock_path(struct TrashAddr *taddr, char *path, size_t pathLen) {
    if(taddr == NULL || taddr->addr.ss_family != AF_UNIX) {
        return;
    }

    struct sockaddr_un *addr;
    addr = (struct sockaddr_un *) &taddr->addr;
    if(pathLen >= sizeof(addr->sun_path)) {
        memcpy(path, addr->sun_path, sizeof(addr->sun_path));
    }
}

void set_unix_trash_addr(struct TrashAddr *taddr, const char *path) {
    if(path != NULL && taddr != NULL) {
        memset(taddr, 0, sizeof(struct TrashAddr));
        taddr->addr.ss_family = AF_UNIX;

        struct sockaddr_un *addr = (struct sockaddr_un *) &taddr->addr;
        addr->sun_family = AF_UNIX;
        
        uint16_t index = 0;
        if(path[0] == '@') {
            addr->sun_path[0] = '\0';
            index = 1;
        }

        memcpy(&addr->sun_path[index], path + index, sizeof(addr->sun_path));
    }
}

void set_ip_trash_addr(struct TrashAddr *taddr, const struct sockaddr *addr, sa_family_t sockFam) {
    taddr->addr.ss_family = sockFam;
    switch (sockFam) {
        case AF_INET:
            taddr->addrlen = sizeof(struct sockaddr_in);
            memcpy(&taddr->addr, addr, taddr->addrlen);
            break;
        
        case AF_INET6:
            taddr->addrlen = sizeof(struct sockaddr_in6);
            memcpy(&taddr->addr, addr, taddr->addrlen);
            break;

        default:
            return;
    }
}

const char *parse_sock_addr(struct sockaddr *addr) {
    const char *pAddr;

    switch (addr->sa_family) {
        case AF_UNIX:
            pAddr = "unix socket";
            break;

        case AF_INET:
            char pAddrBuff[INET_ADDRSTRLEN];
            memset(pAddrBuff, 0, sizeof(pAddrBuff));

            pAddr = inet_ntop(AF_INET, addr->sa_data, pAddrBuff, INET_ADDRSTRLEN);
            break;
        
        default:
            pAddr = "rip";
            break;
    }

    return pAddr;
}

uint32_t char_to_uint32(char *bytes) {
    uint32_t val = 0;
    uint32_t len = sizeof(uint32_t);

    for (size_t i = 0; i < len; i++) {
        val |= bytes[i] << (((len - 1) - i) * 8);        
    }

    return ntohl(val);
}
