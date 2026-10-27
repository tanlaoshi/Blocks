/*
 * Command.c — SDHCI 复位/时钟/发令（bd-pi-sd）
 */
#include "Sdhci/Private.h"

int SdhciSoftwareReset(UINT8 Mask) {
    /* Arasan 冷启/无钟时 ALL 可能 >100ms；CMD|DATA 仍用短窗 */
    UINT32 WaitMs = (Mask & SDHCI_RESET_ALL) != 0 ? 250u : WAIT_MILLISECONDS;

    SdhciWrite8(SDHCI_SOFTWARE_RESET, Mask);
    return SdhciWaitMillisecondsMask8(SDHCI_SOFTWARE_RESET, Mask, 0, WaitMs);
}

/* 对齐 U-Boot sdhci_set_clock（v2 幂次 / v3 偶除数） */
int SdhciClockConfigure(UINT32 Hertz) {
    UINT32 Div;
    UINT16 Clk;

    SdhciWrite16(SDHCI_CLOCK_CONTROL, 0);
    if (Hertz == 0 || gSdhciBaseClockHertz == 0) {
        return -1;
    }
    if (gSdhciHostSpecification >= SDHCI_SPEC_300) {
        if (Hertz >= gSdhciBaseClockHertz) {
            Div = 1;
        } else {
            for (Div = 2; Div < SDHCI_MAX_DIV_300; Div += 2) {
                if ((gSdhciBaseClockHertz / Div) <= Hertz) {
                    break;
                }
            }
        }
        Div >>= 1;
    } else {
        for (Div = 1; Div < SDHCI_MAX_DIV_200; Div *= 2) {
            if ((gSdhciBaseClockHertz / Div) <= Hertz) {
                break;
            }
        }
        Div >>= 1;
    }

    Clk = (UINT16)(((Div & SDHCI_DIV_MASK) << SDHCI_DIVIDER_SHIFT) |
                   (((Div & SDHCI_DIV_HI_MASK) >> SDHCI_DIV_MASK_LEN)
                    << SDHCI_DIVIDER_HI_SHIFT) |
                   SDHCI_CLOCK_INT_EN);
    SdhciWrite16(SDHCI_CLOCK_CONTROL, Clk);
    if (SdhciWaitMillisecondsMask32(SDHCI_CLOCK_CONTROL, SDHCI_CLOCK_INT_STABLE, 1, 20) != 0) {
        return -1;
    }
    SdhciWrite16(SDHCI_CLOCK_CONTROL, (UINT16)(Clk | SDHCI_CLOCK_CARD_EN));
    SdhciDelayMicroseconds(1000);
    return 0;
}

int SdhciCommandSend(UINT8 CommandIndex, UINT32 Argument, UINT16 Flags, UINT16 TransferMode) {
    UINT32 St;
    UINT32 n;

    if (SdhciWaitMillisecondsMask32(SDHCI_PRESENT_STATE, SDHCI_CMD_INHIBIT | SDHCI_DATA_INHIBIT, 0,
                     WAIT_MILLISECONDS) != 0) {
        gSdhciLastPresentState = SdhciRead32(SDHCI_PRESENT_STATE);
        gSdhciLastCommandStatus = SdhciRead32(SDHCI_INT_STATUS);
        return -1;
    }
    SdhciWrite32(SDHCI_INT_STATUS, 0xFFFFFFFFu);
    SdhciWrite32(SDHCI_ARGUMENT, Argument);
    SdhciWrite16Pair(SDHCI_TRANSFER_MODE, TransferMode, (UINT16)SDHCI_MAKE_CMD(CommandIndex, Flags));

    for (n = 0; n < WAIT_MILLISECONDS * 10u; n++) {
        St = SdhciRead32(SDHCI_INT_STATUS);
        if ((St & SDHCI_INT_ERROR) != 0) {
            gSdhciLastCommandStatus = St;
            gSdhciLastPresentState = SdhciRead32(SDHCI_PRESENT_STATE);
            SdhciWrite32(SDHCI_INT_STATUS, St);
            SdhciSoftwareReset(SDHCI_RESET_CMD | SDHCI_RESET_DATA);
            return -1;
        }
        if ((St & SDHCI_INT_RESPONSE) != 0) {
            break;
        }
        SdhciDelayMicroseconds(100);
    }
    if (n >= WAIT_MILLISECONDS * 10u) {
        gSdhciLastCommandStatus = SdhciRead32(SDHCI_INT_STATUS);
        gSdhciLastPresentState = SdhciRead32(SDHCI_PRESENT_STATE);
        SdhciSoftwareReset(SDHCI_RESET_CMD | SDHCI_RESET_DATA);
        return -1;
    }
    gSdhciResponse[0] = SdhciRead32(SDHCI_RESPONSE + 0);
    gSdhciResponse[1] = SdhciRead32(SDHCI_RESPONSE + 4);
    gSdhciResponse[2] = SdhciRead32(SDHCI_RESPONSE + 8);
    gSdhciResponse[3] = SdhciRead32(SDHCI_RESPONSE + 12);
    SdhciWrite32(SDHCI_INT_STATUS, SDHCI_INT_RESPONSE);
    return 0;
}

