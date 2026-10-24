/*
 * Net.c — libNet（用户态 TCP / DNS 薄封装）
 */
#include <errno.h>
#include <string.h>
#include <Net.h>
#include <unistd.h>
#include <Blocks/File.h>
#include <Blocks/syscall.h>

/* syscall 返回 -(errno) 时贯通；-1 仍映射 EIO */
static void NetFail(long R) {
    int E;

    if (R >= 0) {
        return;
    }
    E = (int)(-R);
    if (E >= 1 && E < 256) {
        errno = E;
    } else {
        errno = EIO;
    }
}

int socket(int domain, int type, int protocol) {
    long r = blocks_socket(domain, type, protocol);
    if (r < 0) {
        NetFail(r == -1 ? (long)-EIO : r);
        return -1;
    }
    return (int)r;
}

int NetConnect(int fd, unsigned ip, unsigned short port) {
    long r;
    if (fd < 0) {
        errno = EBADF;
        return -1;
    }
    r = blocks_connect(fd, (long)ip, (long)port);
    if (r < 0) {
        NetFail(r);
        return -1;
    }
    return 0;
}

int NetBind(int fd, unsigned ip, unsigned short port) {
    long r;
    if (fd < 0) {
        errno = EBADF;
        return -1;
    }
    r = blocks_bind(fd, (long)ip, (long)port);
    if (r < 0) {
        NetFail(r);
        return -1;
    }
    return 0;
}

int listen(int fd, int backlog) {
    long r;
    if (fd < 0) {
        errno = EBADF;
        return -1;
    }
    r = blocks_listen(fd, backlog);
    if (r < 0) {
        NetFail(r);
        return -1;
    }
    return 0;
}

int accept(int fd) {
    long r;
    if (fd < 0) {
        errno = EBADF;
        return -1;
    }
    r = blocks_accept(fd);
    if (r < 0) {
        NetFail(r);
        return -1;
    }
    return (int)r;
}

ssize_t send(int fd, const void *buf, size_t len, int flags) {
    (void)flags;
    return FileWrite(fd, buf, len);
}

ssize_t recv(int fd, void *buf, size_t len, int flags) {
    long R;

    if (flags & MSG_DONTWAIT) {
        R = blocks_recv_nb(fd, buf, (long)len);
        if (R == -EAGAIN) {
            errno = EAGAIN;
            return -1;
        }
        if (R < 0) {
            NetFail(R);
            return -1;
        }
        return (ssize_t)R; /* 0=EOF */
    }
    return FileRead(fd, buf, len);
}

void NetAddrIn(NET_SOCK_ADDR_IN *Sa, unsigned Ip, unsigned Port) {
    if (!Sa) {
        return;
    }
    Sa->Family = AF_INET;
    Sa->Port = (unsigned short)Port;
    Sa->Addr = Ip;
}

int NetConnectIn(int Fd, const NET_SOCK_ADDR_IN *Sa) {
    if (!Sa || Sa->Family != AF_INET) {
        errno = EINVAL;
        return -1;
    }
    return NetConnect(Fd, Sa->Addr, (unsigned short)Sa->Port);
}

int NetBindIn(int Fd, const NET_SOCK_ADDR_IN *Sa) {
    if (!Sa || Sa->Family != AF_INET) {
        errno = EINVAL;
        return -1;
    }
    return NetBind(Fd, Sa->Addr, (unsigned short)Sa->Port);
}

int NetGetAddrIn(NET_SOCK_ADDR_IN *Sa, const char *Name, unsigned Port) {
    unsigned Ip;

    if (!Sa) {
        errno = EINVAL;
        return -1;
    }
    if (NetResolve(Name, &Ip) != 0) {
        return -1;
    }
    NetAddrIn(Sa, Ip, Port);
    return 0;
}

/* 点分 IPv4 / localhost 本地完成；域名走 SYS_NET_SOCKET type=NET_SOCK_RESOLVE */
int NetResolve(const char *Name, unsigned *OutIp) {
    unsigned A = 0, B = 0, C = 0, D = 0;
    unsigned I;
    const char *P;
    unsigned *Slot;
    unsigned Acc;
    int Dots;
    NET_DNS_QUERY Query;
    long R;

    if (!Name || !Name[0] || !OutIp) {
        errno = EINVAL;
        return -1;
    }
    if (Name[0] == 'l' && Name[1] == 'o' && Name[2] == 'c' && Name[3] == 'a' &&
        Name[4] == 'l' && Name[5] == 'h' && Name[6] == 'o' && Name[7] == 's' &&
        Name[8] == 't' && Name[9] == 0) {
        *OutIp = NetIpv4(127, 0, 0, 1);
        return 0;
    }
    P = Name;
    Slot = &A;
    Acc = 0;
    Dots = 0;
    for (I = 0;; I++) {
        char Ch = P[I];
        if (Ch >= '0' && Ch <= '9') {
            Acc = Acc * 10u + (unsigned)(Ch - '0');
            if (Acc > 255u) {
                errno = EINVAL;
                return -1;
            }
            continue;
        }
        if (Ch == '.') {
            *Slot = Acc;
            Acc = 0;
            Dots++;
            if (Dots == 1) {
                Slot = &B;
            } else if (Dots == 2) {
                Slot = &C;
            } else if (Dots == 3) {
                Slot = &D;
            } else {
                errno = EINVAL;
                return -1;
            }
            continue;
        }
        if (Ch == 0) {
            if (Dots != 3) {
                break;
            }
            *Slot = Acc;
            *OutIp = NetIpv4(A, B, C, D);
            return 0;
        }
        break;
    }

    memset(&Query, 0, sizeof(Query));
    for (I = 0; Name[I] != 0; I++) {
        if (I >= NET_NAME_MAX) {
            errno = EINVAL;
            return -1;
        }
        Query.Name[I] = Name[I];
    }
    R = blocks_socket(AF_INET, NET_SOCK_RESOLVE, (long)&Query);
    if (R < 0) {
        NetFail(R == -1 ? (long)-EIO : R);
        return -1;
    }
    *OutIp = Query.Ip;
    return 0;
}
