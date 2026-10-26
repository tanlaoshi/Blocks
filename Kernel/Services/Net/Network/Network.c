/*
 * Network.c — 模块表 Network（Full / VirtDesktop / ConsoleOnly+HAS_NET）
 *
 * 桌面：boot-net-defer — 开机只建教学 UDP/TCP；Probe NET 在 Worker（HalNetInitialize）。
 * ConsoleOnly（Duo 等）：无 Worker → 本模块直接 HalNetInitialize，否则板网永不 Probe。
 * HAL 探测入口 NetInitialize ≠ 本模块 NetworkInitialize。
 */
#include "KernelModulesPrivate.h"
#include "Udp.h"
#include "Tcp.h"
#include "Hal.h"
#include "HalDevices.h"

int NetworkInitialize(void) {
    UdpInitialize();
    TcpInitialize();
    if (HalConsoleOnly()) {
        (void)HalNetInitialize();
    }
    return 0;
}
