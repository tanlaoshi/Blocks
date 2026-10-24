/*
 * socket.c — selfhost：sockaddr connect/bind 轨已拆除。
 * 用 NetConnect/NetBind；socket/send/recv 在 libToyNet。
 */
#include <errno.h>

/* 占位：避免空翻译单元告警；符号已从公开头移除 */
int BlocksSocketPosixTrackRemoved(void) {
    errno = ENOSYS;
    return -1;
}
