/*
 * Errno.h — 内核与用户态约定的错误码数值（PR-N-dns）
 *
 * 数值对齐常见 Linux/x86；用户头 User/include/errno.h 须保持同号。
 * 系统调用失败可返回 -(Errno)，CRT 再置 errno。
 */
#ifndef ABI_ERRNO_H
#define ABI_ERRNO_H

#define EPERM            1
#define ENOENT           2
#define ESRCH            3
#define EIO              5
#define EAGAIN          11
#define ENOMEM          12
#define EACCES          13
#define EEXIST          17
#define ENOTDIR         20
#define EINVAL          22
#define EMFILE          24
#define ENOSPC          28
#define ESPIPE          29
#define EPIPE           32
#define ENOSYS          38
#define ECHILD          10
#define EBADF            9

#define ENETUNREACH    101
#define ECONNRESET     104
#define ENOTCONN       107
#define ETIMEDOUT      110
#define ECONNREFUSED   111
#define EHOSTUNREACH   113
#define EINPROGRESS    115

#endif
