/*
 * MemoryState.c — 存储核门控位一次性快照（pwifi-2）
 *
 * 一次启动把 bwalloc / biststat / standbyctrl / workaround / pwrctl
 * 和五个包装器的 IOCTL 全部打出来，避免「一次启动只排除一个嫌疑」。
 */
#include "Wifi/Private.h"

/*
 * 一次性把存储核的「可能门控位」全打出来，避免一次启动只问一个问题：
 * bwalloc / standbyctrl / biststat / pwrctl / workaround + 五个包装器的 IOCTL。
 */
void WifiDiagnosticsSocramState(void) {
    static const UINT32 Registers[5] = {0x04u, 0x0Cu, 0x14u, 0x1E4u, 0x1E8u};
    static const UINT32 Wrappers[5] = {
        WIFI_CHIPCOMMON_BASE + WIFI_WRAPPER_OFFSET, WIFI_D11_WRAPPER,
        WIFI_SDIO_CORE_BASE + WIFI_WRAPPER_OFFSET, WIFI_ARM_WRAPPER, WIFI_SOCRAM_WRAPPER};
    UINT32 Index;
    UINT32 Value;

    HalSerialBootMarkForce("Boot: Pi WIFI memregs");
    for (Index = 0; Index < 5u; Index++) {
        Value = 0;
        (void)WifiBackplaneRead32(WIFI_SOCRAM_CORE_BASE + Registers[Index], &Value);
        WifiBootMarkHexadecimal32(" ", Value);
    }
    HalSerialBootMarkForce("\nBoot: Pi WIFI ioctl");
    for (Index = 0; Index < 5u; Index++) {
        Value = 0;
        (void)WifiBackplaneRead32(Wrappers[Index] + WIFI_AI_IO_CONTROL_OFFSET, &Value);
        WifiBootMarkHexadecimal32(" ", Value);
    }
    HalSerialBootMarkForce("\n");
    /*
     * bwalloc 在正常芯片上是 0（无窗口分配），实测却是 e5e5e5e5。
     * 先回读确认它可写，再置 0——若存储窗口解码依赖它，这一步就能放开阵列访问。
     */
    Value = 0;
    (void)WifiBackplaneWrite32(WIFI_SOCRAM_CORE_BASE + 0x04u, 0u);
    (void)WifiBackplaneRead32(WIFI_SOCRAM_CORE_BASE + 0x04u, &Value);
    WifiBootMarkHexadecimal32("Boot: Pi WIFI bwalloc set0 read=", Value);
    (void)WifiBackplaneWrite32(WIFI_SOCRAM_CORE_BASE + 0x14u, 0u);
    (void)WifiBackplaneRead32(WIFI_SOCRAM_CORE_BASE + 0x14u, &Value);
    WifiBootMarkHexadecimal32(" standby set0 read=", Value);
    HalSerialBootMarkForce("\n");
}

/* LPO：GPCLK2 的控制/分频寄存器，读一下就知道 32.768 kHz 到底在不在 */
void WifiDiagnosticsClockState(void) {
    volatile UINT32 *Control = (volatile UINT32 *)(UINTN)(
        WIFI_CLOCK_MANAGER_BASE + WIFI_CLOCK_MANAGER_GP2_CONTROL_OFFSET);
    volatile UINT32 *Divider = (volatile UINT32 *)(UINTN)(
        WIFI_CLOCK_MANAGER_BASE + WIFI_CLOCK_MANAGER_GP2_DIVIDER_OFFSET);

    WifiBootMarkHexadecimal32("Boot: Pi WIFI lpo ctl=", *Control);
    WifiBootMarkHexadecimal32(" div=", *Divider);
    HalSerialBootMarkForce("\n");
}

/*
 * 三个别名逐个试读，返回第一个读得通的基址（读失败不会楔住卡，可在一次启动内全试完）。
 * Linux 自己用的是 0x00000000 —— 那个地址在 ALP 序列修正之后从未重测过。
 */
UINT32 WifiSramFindAccessibleBase(void) {
    static const UINT32 Candidates[3] = {0x00000000u, 0x10000000u, 0x1e000000u};
    UINT32 Index;
    UINT32 Value;

    for (Index = 0; Index < 3u; Index++) {
        Value = 0;
        if (WifiBackplaneRead32(Candidates[Index] + 0x100u, &Value) == 0) {
            WifiBootMarkHexadecimal32("Boot: Pi WIFI sram base ok=", Candidates[Index]);
            WifiBootMarkHexadecimal32(" value=", Value);
            HalSerialBootMarkForce("\n");
            return Candidates[Index];
        }
        WifiBootMarkHexadecimal32("Boot: Pi WIFI sram base no=", Candidates[Index]);
        HalSerialBootMarkForce("\n");
    }
    return 0xFFFFFFFFu;
}
