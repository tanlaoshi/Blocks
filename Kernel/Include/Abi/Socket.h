/*
 * Socket.h — 用户态 socket 常量（简化 ABI；无 sockaddr 轨）
 *
 * SYS_NET_SOCKET / SYS_NET_BIND / SYS_NET_LISTEN / SYS_NET_ACCEPT / SYS_NET_CONNECT
 * SYS_FILE_WRITE / SYS_FILE_READ / SYS_FILE_CLOSE 在 socket fd 上即 send/recv/close
 *
 * 默认 make LWIP=1（可用 LWIP=0 关掉）；首次 socket() 会自动 LwIpInitialize。
 * PR-A-net-dns：type=NET_SOCK_RESOLVE 时 rdx 为 NET_DNS_QUERY*（不占新 syscall 号）。
 */
#ifndef SOCKET_H
#define SOCKET_H

#include "BootTypes.h"

#define AF_INET     2
#define SOCK_STREAM 1
#define INADDR_ANY  0

#define NET_SOCK_RESOLVE 0x100
#define NET_NAME_MAX     127

typedef struct {
    char Name[NET_NAME_MAX + 1];
    UINT32 Ip;
} NET_DNS_QUERY;

#endif
