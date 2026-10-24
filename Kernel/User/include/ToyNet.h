/*
 * ToyNet.h — 用户态 libNet（selfhost：只教 Net* 主机序）
 *
 * NetConnect / NetBind — (fd, ip, port) **主机序**。
 * 勿再用 sockaddr/htons 课设路径。
 */
#ifndef NET_H
#define NET_H

#include <sys/types.h>
#include <sys/socket.h> /* AF_INET / socket / send / recv */

#define NET_ABI_VERSION_MAJOR 3
#define NET_ABI_VERSION_MINOR 0
#define NET_ABI_VERSION_PATCH 0
#define NET_ABI_VERSION_STRING "3.0.0"

/* SYS_NET_SOCKET 的 type：域名查询；应用请用 NetResolve，不要直接 socket 此类型 */
#define NET_SOCK_RESOLVE 0x100
#define NET_NAME_MAX     127

typedef struct {
    char Name[NET_NAME_MAX + 1];
    unsigned Ip;
} NET_DNS_QUERY;

/*
 * NET_SOCK_ADDR_IN — 主机序地址（与 NetConnect(fd, ip, port) 同一约定）。
 * 主机序地址结构；勿与 Linux sockaddr_in 网络序混淆。
 */
typedef struct {
    unsigned short Family;
    unsigned short Port;
    unsigned Addr;
} NET_SOCK_ADDR_IN;

/* 主机序 IPv4：NetIpv4(10,0,2,2) == 0x0A000202（同 NETDEMO） */
static inline unsigned NetIpv4(unsigned A, unsigned B, unsigned C, unsigned D) {
    return ((A & 0xffu) << 24) | ((B & 0xffu) << 16) | ((C & 0xffu) << 8) | (D & 0xffu);
}

/* 主机序 connect/bind */
int NetConnect(int fd, unsigned ip, unsigned short port);
int NetBind(int fd, unsigned ip, unsigned short port);

void NetAddrIn(NET_SOCK_ADDR_IN *Sa, unsigned Ip, unsigned Port);
int NetConnectIn(int Fd, const NET_SOCK_ADDR_IN *Sa);
int NetBindIn(int Fd, const NET_SOCK_ADDR_IN *Sa);
int NetGetAddrIn(NET_SOCK_ADDR_IN *Sa, const char *Name, unsigned Port);
/* 点分 IPv4 / localhost 本地解析；其它名字走内核 lwIP DNS（Guest 需网卡；socket 会 LwIpInitialize） */
int NetResolve(const char *Name, unsigned *OutIp);

#endif
