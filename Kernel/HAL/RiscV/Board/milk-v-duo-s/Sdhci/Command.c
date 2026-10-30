/*
 * Command.c — Duo SDHCI 复位/PHY/时钟/发令（bd-duo-sd）
 */
#include "Sdhci/Private.h"

int SdhciSoftwareReset(SDHCI_HOST *Host, UINT8 Mask) {
    UINT32 IntEnable = SdhciRead32(Host, SDHCI_INT_ENABLE);
    UINT32 SignalEnable = SdhciRead32(Host, SDHCI_SIGNAL_ENABLE);
    int Rc;

    /* Linux sdhci_cvi_reset_helper：先关中断 → 复位 → 恢复（sdhci-cv181x.c:442-454） */
    SdhciWrite32(Host, SDHCI_INT_ENABLE, 0);
    SdhciWrite32(Host, SDHCI_SIGNAL_ENABLE, 0);
    SdhciWrite8(Host, SDHCI_SOFTWARE_RESET, Mask);
    if (SdhciWait8(Host, SDHCI_SOFTWARE_RESET, Mask, 0, 100) != 0) {
        SdhciWrite32(Host, SDHCI_INT_ENABLE, IntEnable);
        SdhciWrite32(Host, SDHCI_SIGNAL_ENABLE, SignalEnable);
        return -1;
    }
    /* Linux cv18xx_sdhci_reset：每次复位后重写 latency / PHY bypass */
    Rc = SdhciPhysicalAfterReset(Host);
    SdhciWrite32(Host, SDHCI_INT_ENABLE, IntEnable);
    SdhciWrite32(Host, SDHCI_SIGNAL_ENABLE, SignalEnable);
    return Rc;
}

/*
 * Linux cv18xx_sdhci_reset：SD 槽不置 EMMC_FUNC_EN。
 * SD1（wifi-sd）额外置 MSHC_CTRL[16]（sdhci-cv181x.c:859-862）。
 */
int SdhciPhysicalAfterReset(SDHCI_HOST *Host) {
    UINT32 Area = SdhciRead32(Host, SDHCI_P_VENDOR_AREA1) & AREA1_MASK;
    UINT32 Ctrl;
    UINT32 Config;

    if (Area < 0x100u || Area + CV_PHY_CONFIG >= 0x1000u) {
        /*
         * 厂商 Linux 不读 0xE8，直接写死厂商区 0x200（sdhci-cv181x.h:39）。
         * 真机上 0xE8 读不出合法值时退回 0x200 继续（SD1 上这一位还没实测过，
         * 时序表 §8-C 第 1 条）。
         */
        SdhciMarkHex("Boot: SD vendor area bad ", Area, 8);
        SdhciMark(" -> 200\n");
        Area = 0x200u;
    }
    Host->VendorArea = Area;
    Ctrl = SdhciRead32(Host, Area + CV_MSHC_CTRL);
    Ctrl |= CV_LATENCY_1T;
    if (Host->Index == 1u) {
        Ctrl |= CV_MSHC_CTRL_SD1; /* 厂商：SD1 的 DS/HS 档也置这一位 */
    }
    SdhciWrite32(Host, Area + CV_MSHC_CTRL, Ctrl);
    Config = SdhciRead32(Host, Area + CV_PHY_CONFIG);
    SdhciWrite32(Host, Area + CV_PHY_CONFIG, Config | CV_PHY_TX_BPS);
    SdhciWrite32(Host, Area + CV_PHY_TX_RX, CV_PHY_DLY_BYPASS);
    return 0;
}

int SdhciClockConfigure(SDHCI_HOST *Host, UINT32 Hertz) {
    UINT32 Div;
    UINT16 Clk;

    SdhciWrite16(Host, SDHCI_CLOCK_CONTROL, 0);
    Host->ClockHertz = Hertz;
    Host->ActualClockHertz = 0;
    if (Hertz == 0) {
        return -1;
    }
    if (Host->Specification >= SDHCI_SPEC_300) {
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
                   (((Div & SDHCI_DIV_HI_MASK) >> SDHCI_DIV_MASK_LEN) << SDHCI_DIVIDER_HI_SHIFT) |
                   SDHCI_CLOCK_INT_EN);
    SdhciWrite16(Host, SDHCI_CLOCK_CONTROL, Clk);
    if (SdhciWait32(Host, SDHCI_CLOCK_CONTROL, SDHCI_CLOCK_INT_STABLE, 1, 100) != 0) {
        return -1;
    }
    SdhciWrite16(Host, SDHCI_CLOCK_CONTROL, (UINT16)(Clk | SDHCI_CLOCK_CARD_EN));
    SdhciWrite8(Host, SDHCI_TIMEOUT_CONTROL, 0x0E);
    Host->ActualClockHertz = (Host->Specification >= SDHCI_SPEC_300)
                                 ? (SD_BASE_CLOCK_HERTZ / (2u * Div))
                                 : (SD_BASE_CLOCK_HERTZ / (Div ? Div : 1u));
    SdhciDelayMicroseconds(1000);
    return 0;
}

int SdhciCommandSend(SDHCI_HOST *Host, UINT8 CommandIndex, UINT32 Argument, UINT16 Flags,
                     UINT16 TransferMode) {
    UINT32 Inhibit = SDHCI_CMD_INHIBIT;
    UINT32 n;
    UINT32 St;

    if ((Flags & SDHCI_CMD_DATA) != 0) {
        Inhibit |= SDHCI_DATA_INHIBIT;
    }
    if (SdhciWait32(Host, SDHCI_PRESENT_STATE, Inhibit, 0, 100) != 0) {
        Host->LastPresentState = SdhciRead32(Host, SDHCI_PRESENT_STATE);
        Host->LastStatus = SdhciRead32(Host, SDHCI_INT_STATUS);
        return -1;
    }
    SdhciWrite32(Host, SDHCI_INT_STATUS, 0xFFFFFFFFu);
    SdhciWrite32(Host, SDHCI_ARGUMENT, Argument);
    SdhciWrite32(Host, SDHCI_TRANSFER_MODE,
                 ((UINT32)SDHCI_MAKE_CMD(CommandIndex, Flags) << 16) | TransferMode);

    for (n = 0; n < 1000u; n++) {
        St = SdhciRead32(Host, SDHCI_INT_STATUS);
        if ((St & SDHCI_INT_ERROR) != 0) {
            Host->LastStatus = St;
            Host->LastPresentState = SdhciRead32(Host, SDHCI_PRESENT_STATE);
            SdhciWrite32(Host, SDHCI_INT_STATUS, St);
            (void)SdhciSoftwareReset(Host, SDHCI_RESET_CMD | SDHCI_RESET_DATA);
            return -1;
        }
        if ((St & SDHCI_INT_RESPONSE) != 0) {
            break;
        }
        SdhciDelayMicroseconds(100);
    }
    if (n >= 1000u) {
        Host->LastStatus = SdhciRead32(Host, SDHCI_INT_STATUS);
        Host->LastPresentState = SdhciRead32(Host, SDHCI_PRESENT_STATE);
        (void)SdhciSoftwareReset(Host, SDHCI_RESET_CMD | SDHCI_RESET_DATA);
        return -1;
    }
    Host->Response[0] = SdhciRead32(Host, SDHCI_RESPONSE);
    Host->Response[1] = SdhciRead32(Host, SDHCI_RESPONSE + 4);
    Host->Response[2] = SdhciRead32(Host, SDHCI_RESPONSE + 8);
    Host->Response[3] = SdhciRead32(Host, SDHCI_RESPONSE + 12);
    SdhciWrite32(Host, SDHCI_INT_STATUS, SDHCI_INT_RESPONSE);
    if ((Flags & 0x03u) == SDHCI_CMD_RESP_SHORT_BUSY) {
        if (SdhciWait32(Host, SDHCI_INT_STATUS, SDHCI_INT_DATA_END | SDHCI_INT_ERROR, 1, 1000) !=
            0) {
            Host->LastStatus = SdhciRead32(Host, SDHCI_INT_STATUS);
            return -1;
        }
        if ((SdhciRead32(Host, SDHCI_INT_STATUS) & SDHCI_INT_ERROR) != 0) {
            Host->LastStatus = SdhciRead32(Host, SDHCI_INT_STATUS);
            SdhciWrite32(Host, SDHCI_INT_STATUS, Host->LastStatus);
            (void)SdhciSoftwareReset(Host, SDHCI_RESET_CMD | SDHCI_RESET_DATA);
            return -1;
        }
        SdhciWrite32(Host, SDHCI_INT_STATUS, SDHCI_INT_DATA_END);
    }
    return 0;
}
