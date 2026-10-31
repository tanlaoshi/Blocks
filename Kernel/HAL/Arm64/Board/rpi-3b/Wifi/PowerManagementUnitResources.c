/*
 * PowerManagementUnitResources.c — PMU min/max resource 与 SOCRAM 再复位
 *
 * 0x139：min←ffffffff 后 res≈7fffffff，阵列仍 DTO。
 * 0x13a：疑 SOCRAM 在资源未齐时已复位；资源拉齐后再 CoreReset + bankpda。
 */
#include "Wifi/Private.h"

int WifiPowerManagementUnitForceResources(void) {
    UINT32 PowerManagementStatus = 0;
    UINT32 ResourceStatus = 0;
    UINT32 MinimumResourceMask = 0;
    UINT32 MaximumResourceMask = 0;
    UINT32 Attempt;

    if (WifiBackplaneRead32(WIFI_CHIPCOMMON_BASE +
                                WIFI_CHIPCOMMON_POWER_MANAGEMENT_UNIT_STATUS_OFFSET,
                            &PowerManagementStatus) != 0 ||
        WifiBackplaneRead32(
            WIFI_CHIPCOMMON_BASE +
                WIFI_CHIPCOMMON_POWER_MANAGEMENT_UNIT_RESOURCE_STATUS_OFFSET,
            &ResourceStatus) != 0 ||
        WifiBackplaneRead32(
            WIFI_CHIPCOMMON_BASE +
                WIFI_CHIPCOMMON_POWER_MANAGEMENT_UNIT_MINIMUM_RESOURCE_MASK_OFFSET,
            &MinimumResourceMask) != 0 ||
        WifiBackplaneRead32(
            WIFI_CHIPCOMMON_BASE +
                WIFI_CHIPCOMMON_POWER_MANAGEMENT_UNIT_MAXIMUM_RESOURCE_MASK_OFFSET,
            &MaximumResourceMask) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI pmu res read fail\n");
        return -1;
    }
    WifiBootMarkHexadecimal32("Boot: Pi WIFI pmu stat=", PowerManagementStatus);
    WifiBootMarkHexadecimal32(" res=", ResourceStatus);
    HalSerialBootMarkForce("\n");
    WifiBootMarkHexadecimal32("Boot: Pi WIFI pmu min=", MinimumResourceMask);
    WifiBootMarkHexadecimal32(" max=", MaximumResourceMask);
    HalSerialBootMarkForce("\n");
    /* 先抬 max，再抬 min——否则 min 位可能被 max 裁掉 */
    if (WifiBackplaneWrite32(
            WIFI_CHIPCOMMON_BASE +
                WIFI_CHIPCOMMON_POWER_MANAGEMENT_UNIT_MAXIMUM_RESOURCE_MASK_OFFSET,
            0xFFFFFFFFu) != 0 ||
        WifiBackplaneWrite32(
            WIFI_CHIPCOMMON_BASE +
                WIFI_CHIPCOMMON_POWER_MANAGEMENT_UNIT_MINIMUM_RESOURCE_MASK_OFFSET,
            0xFFFFFFFFu) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI pmu min/max set fail\n");
        return -1;
    }
    for (Attempt = 0; Attempt < 50u; Attempt++) {
        if (WifiBackplaneRead32(
                WIFI_CHIPCOMMON_BASE +
                    WIFI_CHIPCOMMON_POWER_MANAGEMENT_UNIT_RESOURCE_STATUS_OFFSET,
                &ResourceStatus) != 0) {
            HalSerialBootMarkForce("Boot: Pi WIFI pmu res poll fail\n");
            return -1;
        }
        if (ResourceStatus == 0xFFFFFFFFu) {
            break;
        }
        SdhciDelayMicroseconds(1000);
    }
    if (WifiBackplaneRead32(
            WIFI_CHIPCOMMON_BASE +
                WIFI_CHIPCOMMON_POWER_MANAGEMENT_UNIT_MINIMUM_RESOURCE_MASK_OFFSET,
            &MinimumResourceMask) != 0) {
        return -1;
    }
    WifiBootMarkHexadecimal32("Boot: Pi WIFI pmu min set=", MinimumResourceMask);
    WifiBootMarkHexadecimal32(" res=", ResourceStatus);
    HalSerialBootMarkForce("\n");
    return 0;
}

/*
 * 资源已齐后再复位 SOCRAM：对表 cm3_set_passive 的 SOCRAM 段，但顺序在 PMU 之后。
 */
int WifiSocramResetAfterResources(void) {
    UINT32 IoControl = 0;
    UINT32 ResetControl = 0;
    UINT32 BankPda = 0;

    if (WifiCoreReset(WIFI_SOCRAM_WRAPPER, 0, 0, 0) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI SOCRAM re-reset fail\n");
        return -1;
    }
    if (WifiBackplaneWrite32(WIFI_SOCRAM_BANK_INDEX, 3u) != 0 ||
        WifiBackplaneWrite32(WIFI_SOCRAM_BANK_PDA, 0u) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI bank remap re-off fail\n");
        return -1;
    }
    if (WifiBackplaneRead32(WIFI_SOCRAM_BANK_PDA, &BankPda) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI bankpda rb fail\n");
        return -1;
    }
    (void)WifiBackplaneRead32(WIFI_SOCRAM_WRAPPER + WIFI_AI_IO_CONTROL_OFFSET, &IoControl);
    (void)WifiBackplaneRead32(WIFI_SOCRAM_WRAPPER + WIFI_AI_RESET_CONTROL_OFFSET,
                              &ResetControl);
    WifiBootMarkHexadecimal32("Boot: Pi WIFI socram re io=", IoControl);
    WifiBootMarkHexadecimal32(" rst=", ResetControl);
    WifiBootMarkHexadecimal32(" pda=", BankPda);
    HalSerialBootMarkForce("\n");
    return 0;
}
