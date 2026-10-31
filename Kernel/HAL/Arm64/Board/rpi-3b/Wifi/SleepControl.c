/*
 * SleepControl.c — F1 SLEEPCSR / Keep-SDIO-On（对表 Linux 灌片前序）
 *
 * Linux brcmf_sdio_kso_control(on)：写 KSO=1 → settle 2–3ms → poll 读到
 * KSO|DEVON；读失败累计 >5 才弃；try 上限约 PMU_MAX_TRANSITION_DLY/50µs。
 *
 * 0x12b：首写已过；poll 仍走 WifiSdioCommand52Read，把 r5=0x2801（仅 ERROR）
 * 当死错，5 次即 `kso read fail`——从未进入「读出 0x01 再等 DEVON」环。
 * 本刀：poll 读与首写同，容忍仅 ERROR 并取 data 字节；attempts 对齐 Linux。
 */
#include "Wifi/Private.h"

#define WIFI_SDIO_SLEEP_KEEP_SDIO_ON 0x01u
#define WIFI_SDIO_SLEEP_DEVICE_ON    0x02u
#define WIFI_SDIO_SLEEP_AWAKE_MASK \
    (WIFI_SDIO_SLEEP_KEEP_SDIO_ON | WIFI_SDIO_SLEEP_DEVICE_ON)
/* 对表 Linux：PMU_MAX_TRANSITION_DLY≈1s / KSO_WAIT_US=50 → 20000 */
#define WIFI_SDIO_SLEEP_WAKE_ATTEMPTS 20000u
#define WIFI_SDIO_SLEEP_WAKE_GAP_MICROSECONDS 50u
#define WIFI_SDIO_SLEEP_WAKE_SETTLE_MICROSECONDS 2000u
#define WIFI_SDIO_SLEEP_ACCESS_ERROR_LIMIT 5u
#define WIFI_SDIO_SLEEP_R5_HARD_MASK 0xC700u

static void WifiSdioSleepControlReportFailure(const char *Stage) {
    HalSerialBootMarkForce("Boot: Pi WIFI kso ");
    HalSerialBootMarkForce(Stage);
    WifiBootMarkHexadecimal32(" r5=", gSdhciResponse[0]);
    WifiBootMarkHexadecimal32(" isr=", gSdhciLastCommandStatus);
    WifiBootMarkHexadecimal32(" pst=", gSdhciLastPresentState);
    WifiBootMarkHexadecimal32(" clk=", SdhciRead16(SDHCI_CLOCK_CONTROL));
    HalSerialBootMarkForce("\n");
}

static int WifiSdioSleepControlCommand52Direct(int Write, UINT8 DataByte, UINT8 *OutData) {
    UINT16 Flags = SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX;
    UINT32 Argument = ((SDIO_FUNCTION_1 & 7u) << 28) |
                      ((WIFI_SDIO_SLEEP_CONTROL & 0x1FFFFu) << 9) |
                      ((UINT32)DataByte & 0xFFu);
    UINT32 Attempt;

    if (Write) {
        Argument |= (1u << 31);
    }
    for (Attempt = 0; Attempt < 20u; Attempt++) {
        if (SdhciCommandSend((UINT8)SDIO_COMMAND_IO_RW_DIRECT, Argument, Flags, 0) != 0) {
            SdhciDelayMicroseconds(WIFI_SDIO_SLEEP_WAKE_GAP_MICROSECONDS);
            continue;
        }
        if ((gSdhciResponse[0] & WIFI_SDIO_SLEEP_R5_HARD_MASK) != 0) {
            SdhciDelayMicroseconds(WIFI_SDIO_SLEEP_WAKE_GAP_MICROSECONDS);
            continue;
        }
        if (OutData != 0) {
            *OutData = (UINT8)(gSdhciResponse[0] & 0xFFu);
        }
        return 0;
    }
    gSdhciLastCommandStatus = SdhciRead32(SDHCI_INT_STATUS);
    gSdhciLastPresentState = SdhciRead32(SDHCI_PRESENT_STATE);
    return -1;
}

int WifiSdioSleepControlEnsureAwake(void) {
    UINT8 Status = 0;
    UINT32 Attempt;
    UINT32 AccessErrors = 0;

    if (WifiSdioSleepControlCommand52Direct(1, WIFI_SDIO_SLEEP_KEEP_SDIO_ON, 0) != 0) {
        WifiSdioSleepControlReportFailure("write fail");
        return -1;
    }
    SdhciDelayMicroseconds(WIFI_SDIO_SLEEP_WAKE_SETTLE_MICROSECONDS);

    for (Attempt = 0; Attempt < WIFI_SDIO_SLEEP_WAKE_ATTEMPTS; Attempt++) {
        if (WifiSdioSleepControlCommand52Direct(0, 0, &Status) != 0) {
            AccessErrors++;
            if (AccessErrors > WIFI_SDIO_SLEEP_ACCESS_ERROR_LIMIT) {
                WifiSdioSleepControlReportFailure("read fail");
                return -1;
            }
        } else {
            AccessErrors = 0;
            if ((Status & WIFI_SDIO_SLEEP_AWAKE_MASK) == WIFI_SDIO_SLEEP_AWAKE_MASK) {
                WifiBootMarkHexadecimal32("Boot: Pi WIFI kso wake=", Status);
                HalSerialBootMarkForce("\n");
                WifiBackplaneWindowInvalidate();
                return 0;
            }
        }
        (void)WifiSdioSleepControlCommand52Direct(1, WIFI_SDIO_SLEEP_KEEP_SDIO_ON, 0);
        SdhciDelayMicroseconds(WIFI_SDIO_SLEEP_WAKE_GAP_MICROSECONDS);
    }
    WifiBootMarkHexadecimal32("Boot: Pi WIFI kso wake timeout=", Status);
    HalSerialBootMarkForce("\n");
    return -1;
}
