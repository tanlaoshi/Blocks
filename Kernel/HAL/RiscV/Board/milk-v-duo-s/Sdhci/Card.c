/*
 * Card.c — Duo SDHCI 主机/卡初始化（bd-duo-sd）
 *
 * SD0：SD 卡流程（CMD0/8/55/41/2/3/7/16 + ACMD6）。PR-30 只是把全局量换成
 *       SDHCI_HOST*，命令序列与时钟（25 MHz）保持原样，不动启动盘。
 * SD1：SDIO 流程在 Sdio.c，不走本文件的 ACMD41/CMD2。
 */
#include "Sdhci/Private.h"

void SdhciFail(SDHCI_HOST *Host, const char *What) {
    SdhciMark(What);
    SdhciMarkHex(" st=", Host->LastStatus, 8);
    SdhciMarkHex(" pst=", Host->LastPresentState, 8);
    SdhciMark("\n");
}

/*
 * 控制器上电 + 时钟（复位之后调）。
 * IntEnable：SD0 沿用原来的 0xFFFFFFFF；SD1 按规范先全关（SIGNAL_ENABLE 恒 0）。
 */
int SdhciControllerBringUp(SDHCI_HOST *Host, UINT32 Hertz, UINT32 IntEnable) {
    SdhciWrite8(Host, SDHCI_POWER_CONTROL, (UINT8)(SDHCI_POWER_ON | SDHCI_POWER_330));
    SdhciDelayMicroseconds(10000);
    SdhciWrite8(Host, SDHCI_HOST_CONTROL, 0);
    SdhciWrite8(Host, SDHCI_TIMEOUT_CONTROL, 0x0E);
    SdhciWrite32(Host, SDHCI_INT_ENABLE, IntEnable);
    SdhciWrite32(Host, SDHCI_SIGNAL_ENABLE, 0);
    /* no-1-8-v：保持 3.3V，不写 HOST_CONTROL2 的 1.8V 位 */
    if (SdhciClockConfigure(Host, Hertz) != 0) {
        return -1;
    }
    SdhciDelayMicroseconds(2000);
    return 0;
}

int SdhciCardInitialize(SDHCI_HOST *Host) {
    UINT16 Fl;
    UINT32 Ocr;
    UINT32 i;
    int Cmd8Ok;
    UINT32 Acmd;

    SdhciMark("Boot: SD reset\n");
    if (SdhciSoftwareReset(Host, SDHCI_RESET_ALL) != 0) {
        SdhciMark("Boot: SD softreset timeout\n");
        return -1;
    }
    SdhciMarkHex("Boot: SD vendor ", Host->VendorArea, 8);
    SdhciMark("\n");

    /* SD0 的 INT_ENABLE 沿用历史值 0xFFFFFFFF（SIGNAL_ENABLE=0，不会真出中断） */
    if (SdhciControllerBringUp(Host, IDENTIFY_HERTZ, 0xFFFFFFFFu) != 0) {
        SdhciMark("Boot: SD clock id fail\n");
        return -1;
    }

    if (SdhciWait32(Host, SDHCI_PRESENT_STATE, SDHCI_CARD_STABLE, 1, 100) != 0 ||
        (SdhciRead32(Host, SDHCI_PRESENT_STATE) & SDHCI_CARD_INSERTED) == 0) {
        SdhciMarkHex("Boot: SD no card pst=", SdhciRead32(Host, SDHCI_PRESENT_STATE), 8);
        SdhciMark("\n");
        return -1;
    }

    (void)SdhciCommandSend(Host, 0, 0, SDHCI_CMD_RESP_NONE, 0);
    SdhciDelayMicroseconds(10000);

    Fl = (UINT16)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX);
    Cmd8Ok = 0;
    if (SdhciCommandSend(Host, 8, 0x1AAu, Fl, 0) == 0 && (Host->Response[0] & 0xFFFu) == 0x1AAu) {
        Cmd8Ok = 1;
    } else {
        SdhciFail(Host, "Boot: SD CMD8 fail");
        (void)SdhciSoftwareReset(Host, SDHCI_RESET_CMD | SDHCI_RESET_DATA);
        (void)SdhciCommandSend(Host, 0, 0, SDHCI_CMD_RESP_NONE, 0);
        SdhciDelayMicroseconds(5000);
    }

    Host->HighCapacity = 0;
    Acmd = Cmd8Ok ? 0x40300000u : 0x00300000u;
    Ocr = 0;
    for (i = 0; i < 100u; i++) {
        Fl = (UINT16)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX);
        if (SdhciCommandSend(Host, 55, 0, Fl, 0) != 0) {
            SdhciFail(Host, "Boot: SD CMD55 fail");
            return -1;
        }
        if (SdhciCommandSend(Host, 41, Acmd, SDHCI_CMD_RESP_SHORT, 0) != 0) {
            SdhciFail(Host, "Boot: SD ACMD41 fail");
            return -1;
        }
        Ocr = Host->Response[0];
        if ((Ocr & 0x80000000u) != 0) {
            Host->HighCapacity = (Ocr & 0x40000000u) ? 1 : 0;
            break;
        }
        SdhciDelayMicroseconds(10000);
    }
    if ((Ocr & 0x80000000u) == 0) {
        SdhciMark("Boot: SD ACMD41 timeout\n");
        return -1;
    }

    if (SdhciClockConfigure(Host, SD_TRANSFER_HERTZ) != 0) {
        SdhciMark("Boot: SD clock xfer fail\n");
        return -1;
    }

    Fl = (UINT16)(SDHCI_CMD_RESP_LONG | SDHCI_CMD_CRC);
    if (SdhciCommandSend(Host, 2, 0, Fl, 0) != 0) {
        SdhciFail(Host, "Boot: SD CMD2 fail");
        return -1;
    }
    Fl = (UINT16)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX);
    if (SdhciCommandSend(Host, 3, 0, Fl, 0) != 0) {
        SdhciFail(Host, "Boot: SD CMD3 fail");
        return -1;
    }
    Host->RelativeCardAddress = Host->Response[0] & 0xFFFF0000u;

    Fl = (UINT16)(SDHCI_CMD_RESP_SHORT_BUSY | SDHCI_CMD_CRC | SDHCI_CMD_INDEX);
    if (SdhciCommandSend(Host, 7, Host->RelativeCardAddress, Fl, 0) != 0) {
        SdhciFail(Host, "Boot: SD CMD7 fail");
        return -1;
    }

    Fl = (UINT16)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX);
    if (SdhciCommandSend(Host, 55, Host->RelativeCardAddress, Fl, 0) == 0 &&
        SdhciCommandSend(Host, 6, 2u, Fl, 0) == 0) {
        SdhciWrite8(Host, SDHCI_HOST_CONTROL, SDHCI_WIDTH_4BIT);
        Host->BusWidth = 4;
    } else {
        SdhciMark("Boot: SD stay 1-bit\n");
    }

    (void)SdhciCommandSend(Host, 16, 512u, Fl, 0);
    Host->CardType = SDHCI_CARD_SD;
    return 0;
}

int SdhciHostInitialize(SDHCI_HOST *Host) {
    UINT32 Cap;

    Host->Specification = (UINT16)(SdhciRead16(Host, SDHCI_HOST_VERSION) & 0xFFu);
    Cap = SdhciRead32(Host, SDHCI_CAPABILITIES);
    SdhciMark("Boot: SDHCI0 ");
    SdhciMarkHex("ver=", Host->Specification, 8);
    SdhciMarkHex(" cap=", Cap, 8);
    SdhciMark("\n");
    return SdhciCardInitialize(Host);
}
