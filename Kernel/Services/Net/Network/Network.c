/*
 * Network.c — 模块表 Network（Full / VirtDesktop；Virt 串口表无此项）
 *
 * boot-net-defer：开机只建教学 UDP/TCP 栈；Probe NET 挪到 Worker（HalNetInitialize）。
 * HAL 探测入口 NetInitialize ≠ 本模块 NetworkInitialize。
 */
#include "KernelModulesPrivate.h"
#include "Udp.h"
#include "Tcp.h"

int NetworkInitialize(void) {
    UdpInitialize();
    TcpInitialize();
    return 0;
}
