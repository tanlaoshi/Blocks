/*
 * sys/socket.h — 套接字常量与 socket/send/recv…（selfhost：无 sockaddr 轨）
 *
 * connect/bind 课设路径已废；用 <Net.h> 的 NetConnect/NetBind（主机序）。
 */
#ifndef SYS_SOCKET_H
#define SYS_SOCKET_H

#include <sys/types.h>
#include <stddef.h>

#define AF_INET      2
#define SOCK_STREAM  1
#define INADDR_ANY   0
#define MSG_DONTWAIT 0x40

int socket(int domain, int type, int protocol);
int listen(int fd, int backlog);
int accept(int fd);
ssize_t send(int fd, const void *buf, size_t len, int flags);
ssize_t recv(int fd, void *buf, size_t len, int flags);

#endif
