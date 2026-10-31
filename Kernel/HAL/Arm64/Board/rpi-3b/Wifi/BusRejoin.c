/*
 * BusRejoin.c — Claim 灌片前 SDIO 重会合（延后 FirmwareLoad）
 *
 * BOOT 探针后到 Worker Claim 可能隔很久。只靠 KSO 只能把 SLEEPCSR 写成 0x01，
 * DEVON 永不回来（tag 0x12c：`kso wake timeout=00000001`）。
 * Linux 在枚举后立刻灌片，无此空窗。延后路径须对表开机：WL_ON POR →
 * host → 枚举 → F1，再走 KSO/ALP（芯片回到与探针后同等的可灌态）。
 * 不重跑 ChipIdentity（gWifiChipProbed / 路径已定）；不清固件探测标志。
 */
#include "Wifi/Private.h"

int WifiSdioBusRejoinAfterIdle(void) {
    HalSerialBootMarkForce("Boot: Pi WIFI bus rejoin\n");
    if (WifiWirelessPowerOn() != 0) {
        return -1;
    }
    if (WifiSdioHostBringUp() != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI rejoin host fail\n");
        return -1;
    }
    if (WifiSdioEnumerate() != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI rejoin enum fail\n");
        return -1;
    }
    if (WifiSdioEnableFunction1() != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI rejoin F1 fail\n");
        return -1;
    }
    WifiBackplaneWindowInvalidate();
    HalSerialBootMarkForce("Boot: Pi WIFI bus rejoin ok\n");
    return 0;
}
