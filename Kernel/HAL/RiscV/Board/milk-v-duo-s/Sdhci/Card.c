/*
 * Card.c — Duo SDHCI 卡/主机初始化（bd-duo-sd）
 */
#include "Sdhci/Private.h"

void SdhciFail(const char *What) {
    SdhciMark(What);
    SdhciMarkHexadecimal32(" st=", gSdhciLastStatus);
    SdhciMarkHexadecimal32(" pst=", gSdhciLastPresentState);
    SdhciMark("\n");
}

int SdhciCardInitialize(void) {
    UINT16 Fl;
    UINT32 Ocr;
    UINT32 i;
    int Cmd8Ok;
    UINT32 Acmd;

    SdhciMark("Boot: SD reset\n");
    if (SdhciSoftwareReset(SDHCI_RESET_ALL) != 0) {
        SdhciMark("Boot: SD softreset timeout\n");
        return -1;
    }
    SdhciMarkHexadecimal32("Boot: SD vendor ", gSdhciVendorArea);
    SdhciMark("\n");
    SdhciWrite8(SDHCI_POWER_CONTROL, (UINT8)(SDHCI_POWER_ON | SDHCI_POWER_330));
    SdhciDelayMicroseconds(10000);
    SdhciWrite8(SDHCI_HOST_CONTROL, 0);
    SdhciWrite8(SDHCI_TIMEOUT_CONTROL, 0x0E);
    SdhciWrite32(SDHCI_INT_ENABLE, 0xFFFFFFFFu);
    SdhciWrite32(SDHCI_SIGNAL_ENABLE, 0);
    /* no-1-8-v：保持 3.3V，不写 HOST_CONTROL2 的 1.8V 位 */

    if (SdhciClockConfigure(IDENTIFY_HERTZ) != 0) {
        SdhciMark("Boot: SD clock id fail\n");
        return -1;
    }
    SdhciDelayMicroseconds(2000);

    if (SdhciWaitMilliseconds32(SDHCI_PRESENT_STATE, SDHCI_CARD_STABLE, 1, 100) != 0 ||
        (SdhciRead32(SDHCI_PRESENT_STATE) & SDHCI_CARD_INSERTED) == 0) {
        SdhciMarkHexadecimal32("Boot: SD no card pst=", SdhciRead32(SDHCI_PRESENT_STATE));
        SdhciMark("\n");
        return -1;
    }

    (void)SdhciCommandSend(0, 0, SDHCI_CMD_RESP_NONE, 0);
    SdhciDelayMicroseconds(10000);

    Fl = (UINT16)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX);
    Cmd8Ok = 0;
    if (SdhciCommandSend(8, 0x1AAu, Fl, 0) == 0 && (gSdhciResponse[0] & 0xFFFu) == 0x1AAu) {
        Cmd8Ok = 1;
    } else {
        SdhciFail("Boot: SD CMD8 fail");
        (void)SdhciSoftwareReset(SDHCI_RESET_CMD | SDHCI_RESET_DATA);
        (void)SdhciCommandSend(0, 0, SDHCI_CMD_RESP_NONE, 0);
        SdhciDelayMicroseconds(5000);
    }

    gSdhciHighCapacity = 0;
    Acmd = Cmd8Ok ? 0x40300000u : 0x00300000u;
    Ocr = 0;
    for (i = 0; i < 100u; i++) {
        Fl = (UINT16)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX);
        if (SdhciCommandSend(55, 0, Fl, 0) != 0) {
            SdhciFail("Boot: SD CMD55 fail");
            return -1;
        }
        if (SdhciCommandSend(41, Acmd, SDHCI_CMD_RESP_SHORT, 0) != 0) {
            SdhciFail("Boot: SD ACMD41 fail");
            return -1;
        }
        Ocr = gSdhciResponse[0];
        if ((Ocr & 0x80000000u) != 0) {
            gSdhciHighCapacity = (Ocr & 0x40000000u) ? 1 : 0;
            break;
        }
        SdhciDelayMicroseconds(10000);
    }
    if ((Ocr & 0x80000000u) == 0) {
        SdhciMark("Boot: SD ACMD41 timeout\n");
        return -1;
    }

    if (SdhciClockConfigure(TRANSFER_HERTZ) != 0) {
        SdhciMark("Boot: SD clock xfer fail\n");
        return -1;
    }

    Fl = (UINT16)(SDHCI_CMD_RESP_LONG | SDHCI_CMD_CRC);
    if (SdhciCommandSend(2, 0, Fl, 0) != 0) {
        SdhciFail("Boot: SD CMD2 fail");
        return -1;
    }
    Fl = (UINT16)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX);
    if (SdhciCommandSend(3, 0, Fl, 0) != 0) {
        SdhciFail("Boot: SD CMD3 fail");
        return -1;
    }
    gSdhciRelativeCardAddress = gSdhciResponse[0] & 0xFFFF0000u;

    Fl = (UINT16)(SDHCI_CMD_RESP_SHORT_BUSY | SDHCI_CMD_CRC | SDHCI_CMD_INDEX);
    if (SdhciCommandSend(7, gSdhciRelativeCardAddress, Fl, 0) != 0) {
        SdhciFail("Boot: SD CMD7 fail");
        return -1;
    }

    Fl = (UINT16)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX);
    if (SdhciCommandSend(55, gSdhciRelativeCardAddress, Fl, 0) == 0 && SdhciCommandSend(6, 2u, Fl, 0) == 0) {
        SdhciWrite8(SDHCI_HOST_CONTROL, SDHCI_WIDTH_4BIT);
    } else {
        SdhciMark("Boot: SD stay 1-bit\n");
    }

    (void)SdhciCommandSend(16, 512u, Fl, 0);
    return 0;
}

int SdhciHostInitialize(void) {
    UINT32 Cap;

    gSdhciHostSpecification = (UINT16)(SdhciRead16(SDHCI_HOST_VERSION) & 0xFFu);
    Cap = SdhciRead32(SDHCI_CAPABILITIES);
    SdhciMark("Boot: SDHCI0 ");
    SdhciMarkHexadecimal32("ver=", gSdhciHostSpecification);
    SdhciMarkHexadecimal32(" cap=", Cap);
    SdhciMark("\n");
    return SdhciCardInitialize();
}
