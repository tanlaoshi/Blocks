/*
 * SdioRecover.c — CMD53 读楔死后的卡侧 abort + 主机恢复（pwifi-2 解封第 3 步）
 */
#include "Wifi/Private.h"

/* 对表 SDIO Simplified Spec：ASx 只 abort 指定功能；
 * F0 的 CMD52 不带数据，DAT 楔死时仍可发。 */
static int WifiSdioAbortFunction1(void) {
    UINT32 Argument = (1u << 31) | ((SDIO_FUNCTION_0 & 7u) << 28) |
                      ((SDIO_CCCR_IO_ABORT & 0x1FFFFu) << 9) | (UINT32)SDIO_FUNCTION_1;
    UINT16 Flags = (UINT16)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX);
    UINT32 Attempt;
    UINT32 Status;

    for (Attempt = 0; Attempt < WAIT_MILLISECONDS * 10u; Attempt++) {
        if ((SdhciRead32(SDHCI_PRESENT_STATE) & SDHCI_CMD_INHIBIT) == 0) {
            break;
        }
        SdhciDelayMicroseconds(100);
    }
    if (Attempt >= WAIT_MILLISECONDS * 10u) {
        return -1;
    }
    SdhciWrite32(SDHCI_INT_STATUS, 0xFFFFFFFFu);
    SdhciWrite32(SDHCI_ARGUMENT, Argument);
    SdhciWrite16Pair(SDHCI_TRANSFER_MODE, 0,
                     (UINT16)SDHCI_MAKE_CMD((UINT8)SDIO_COMMAND_IO_RW_DIRECT, Flags));
    for (Attempt = 0; Attempt < WAIT_MILLISECONDS * 10u; Attempt++) {
        Status = SdhciRead32(SDHCI_INT_STATUS);
        if ((Status & SDHCI_INT_ERROR) != 0) {
            SdhciWrite32(SDHCI_INT_STATUS, Status);
            return -1;
        }
        if ((Status & SDHCI_INT_RESPONSE) != 0) {
            SdhciWrite32(SDHCI_INT_STATUS, SDHCI_INT_RESPONSE);
            return 0;
        }
        SdhciDelayMicroseconds(100);
    }
    return -1;
}

int WifiSdioHostCommandDataRecover(void) {
    /*
     * 楔死的 CMD53 读会把 DATA_INHIBIT 钉住；SdhciCommandSend 起手就等两条
     * inhibit 归零，所以恢复必须走裸发令（WifiSdioAbortFunction1）。
     */
    int Reset;
    int Cleared = 0;
    UINT32 Attempt;

    (void)WifiSdioAbortFunction1();
    Reset = SdhciSoftwareReset(SDHCI_RESET_CMD | SDHCI_RESET_DATA);
    SdhciWrite32(SDHCI_INT_STATUS, 0xFFFFFFFFu);
    WifiBackplaneWindowInvalidate();
    for (Attempt = 0; Attempt < WAIT_MILLISECONDS * 10u; Attempt++) {
        if ((SdhciRead32(SDHCI_PRESENT_STATE) &
             (SDHCI_CMD_INHIBIT | SDHCI_DATA_INHIBIT)) == 0) {
            Cleared = 1;
            break;
        }
        SdhciDelayMicroseconds(100);
    }
    SdhciDelayMicroseconds(1000);
    if (Reset != 0 || Cleared == 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI host recover fail");
        WifiBootMarkHexadecimal32(" pst=", SdhciRead32(SDHCI_PRESENT_STATE));
        HalSerialBootMarkForce("\n");
        return -1;
    }
    return 0;
}
