/*
 * Network.c — 模块表 Network（Full / VirtDesktop / ConsoleOnly+HAS_NET）
 *
 * 桌面 virt/X64：boot-net-defer — 开机只建教学 UDP/TCP；Probe NET 在 Worker。
 * ConsoleOnly（Duo）：无 Worker → 本模块 HalNetInitialize。
 * 真机板网（Pi LAN9514 等 HalNetProbeAtModuleInit=1）：USB 枚举已在 Cpu 完成，
 * Probe/Bind 便宜；Pi CNTV 无 IRQ 时 Worker 可能切不过来 → 必须在此 Probe，
 * 否则 eth ok 却永不 bind、`net` 报 no NIC。
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
    if (HalConsoleOnly() || HalNetProbeAtModuleInit()) {
        (void)HalNetInitialize();
        /*
         * pwifi-2：Pi CNTV poll 时 Worker 可能切不过来（与 eth 同因）。
         * SDIO 灌片须 FS 后；本模块在 FileSystem 之后，在此 Claim。
         * X64 USB WiFi：HalNetProbeAtModuleInit=0，不走这里（仍 Worker/壳）。
         */
        (void)HalWifiClaim();
    }
    return 0;
}
