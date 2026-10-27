/*
 * Command.c — Duo SDHCI 复位/PHY/时钟/发令（bd-duo-sd）
 */
#include "Sdhci/Private.h"

int SdhciSoftwareReset(UINT8 Mask) {
    SdhciWrite8(SDHCI_SOFTWARE_RESET, Mask);
    if (SdhciWaitMilliseconds8(SDHCI_SOFTWARE_RESET, Mask, 0, 100) != 0) {
        return -1;
    }
    /* Linux cv18xx_sdhci_reset：每次复位后重写 latency / PHY bypass */
    return SdhciPhysicalAfterReset();
}

/* Linux cv18xx_sdhci_reset：SD 槽不置 EMMC_FUNC_EN */
int SdhciPhysicalAfterReset(void) {
    UINT32 Area = SdhciRead32(SDHCI_P_VENDOR_AREA1) & AREA1_MASK;
    UINT32 Ctrl;
    UINT32 Cfg;

    if (Area < 0x100u || Area + CV_PHY_CONFIG >= 0x1000u) {
        SdhciMarkHexadecimal32("Boot: SD vendor area bad ", Area);
        SdhciMark("\n");
        return -1;
    }
    gSdhciVendorArea = Area;
    Ctrl = SdhciRead32(Area + CV_MSHC_CTRL);
    SdhciWrite32(Area + CV_MSHC_CTRL, Ctrl | CV_LATENCY_1T);
    Cfg = SdhciRead32(Area + CV_PHY_CONFIG);
    SdhciWrite32(Area + CV_PHY_CONFIG, Cfg | CV_PHY_TX_BPS);
    SdhciWrite32(Area + CV_PHY_TX_RX, CV_PHY_DLY_BYPASS);
    return 0;
}

int SdhciClockConfigure(UINT32 Hertz) {
    UINT32 Div;
    UINT16 Clk;

    SdhciWrite16(SDHCI_CLOCK_CONTROL, 0);
    if (Hertz == 0) {
        return -1;
    }
    if (gSdhciHostSpecification >= SDHCI_SPEC_300) {
        if (Hertz >= SD_BASE_CLOCK_HERTZ) {
            Div = 1;
        } else {
            for (Div = 2; Div < SDHCI_MAX_DIV_300; Div += 2) {
                if ((SD_BASE_CLOCK_HERTZ / Div) <= Hertz) {
                    break;
                }
            }
        }
        Div >>= 1;
    } else {
        for (Div = 1; Div < 256u; Div *= 2) {
            if ((SD_BASE_CLOCK_HERTZ / Div) <= Hertz) {
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
    if (SdhciWaitMilliseconds32(SDHCI_CLOCK_CONTROL, SDHCI_CLOCK_INT_STABLE, 1, 100) != 0) {
        return -1;
    }
    SdhciWrite16(SDHCI_CLOCK_CONTROL, (UINT16)(Clk | SDHCI_CLOCK_CARD_EN));
    SdhciWrite8(SDHCI_TIMEOUT_CONTROL, 0x0E);
    SdhciDelayMicroseconds(1000);
    return 0;
}

int SdhciCommandSend(UINT8 CommandIndex, UINT32 Argument, UINT16 Flags, UINT16 TransferMode) {
    UINT32 Inhibit = SDHCI_CMD_INHIBIT;
    UINT32 n;
    UINT32 St;

    if ((Flags & SDHCI_CMD_DATA) != 0) {
        Inhibit |= SDHCI_DATA_INHIBIT;
    }
    if (SdhciWaitMilliseconds32(SDHCI_PRESENT_STATE, Inhibit, 0, 100) != 0) {
        gSdhciLastPresentState = SdhciRead32(SDHCI_PRESENT_STATE);
        gSdhciLastStatus = SdhciRead32(SDHCI_INT_STATUS);
        return -1;
    }
    SdhciWrite32(SDHCI_INT_STATUS, 0xFFFFFFFFu);
    SdhciWrite32(SDHCI_ARGUMENT, Argument);
    SdhciWrite32(SDHCI_TRANSFER_MODE, ((UINT32)SDHCI_MAKE_CMD(CommandIndex, Flags) << 16) | TransferMode);

    for (n = 0; n < 1000u; n++) {
        St = SdhciRead32(SDHCI_INT_STATUS);
        if ((St & SDHCI_INT_ERROR) != 0) {
            gSdhciLastStatus = St;
            gSdhciLastPresentState = SdhciRead32(SDHCI_PRESENT_STATE);
            SdhciWrite32(SDHCI_INT_STATUS, St);
            (void)SdhciSoftwareReset(SDHCI_RESET_CMD | SDHCI_RESET_DATA);
            return -1;
        }
        if ((St & SDHCI_INT_RESPONSE) != 0) {
            break;
        }
        SdhciDelayMicroseconds(100);
    }
    if (n >= 1000u) {
        gSdhciLastStatus = SdhciRead32(SDHCI_INT_STATUS);
        gSdhciLastPresentState = SdhciRead32(SDHCI_PRESENT_STATE);
        (void)SdhciSoftwareReset(SDHCI_RESET_CMD | SDHCI_RESET_DATA);
        return -1;
    }
    gSdhciResponse[0] = SdhciRead32(SDHCI_RESPONSE);
    gSdhciResponse[1] = SdhciRead32(SDHCI_RESPONSE + 4);
    gSdhciResponse[2] = SdhciRead32(SDHCI_RESPONSE + 8);
    gSdhciResponse[3] = SdhciRead32(SDHCI_RESPONSE + 12);
    SdhciWrite32(SDHCI_INT_STATUS, SDHCI_INT_RESPONSE);
    if ((Flags & 0x03u) == SDHCI_CMD_RESP_SHORT_BUSY) {
        if (SdhciWaitMilliseconds32(SDHCI_INT_STATUS, SDHCI_INT_DATA_END | SDHCI_INT_ERROR, 1,
                     1000) != 0) {
            gSdhciLastStatus = SdhciRead32(SDHCI_INT_STATUS);
            return -1;
        }
        if ((SdhciRead32(SDHCI_INT_STATUS) & SDHCI_INT_ERROR) != 0) {
            gSdhciLastStatus = SdhciRead32(SDHCI_INT_STATUS);
            SdhciWrite32(SDHCI_INT_STATUS, gSdhciLastStatus);
            (void)SdhciSoftwareReset(SDHCI_RESET_CMD | SDHCI_RESET_DATA);
            return -1;
        }
        SdhciWrite32(SDHCI_INT_STATUS, SDHCI_INT_DATA_END);
    }
    return 0;
}
