/*
 * Card.c — SDHCI 卡/主机初始化（bd-pi-sd）
 */
#include "Sdhci/Private.h"

int SdhciCardInitialize(void) {
    UINT32 Ocr;
    UINT32 i;
    UINT16 Fl;
    int Cmd8Ok;
    UINT32 AcmdArg;

    /*
     * 录像：CMD8/55 超时 st=INT_TIMEOUT、pst 有卡。
     * SdhciSoftwareReset ALL 会拆掉 U-Boot 已训好的总线；只清 CMD/DATA，保留电源/钟树。
     */
    HalSerialBootMark("Boot: SDHCI reset cmd|data\n");
    if (SdhciSoftwareReset(SDHCI_RESET_CMD | SDHCI_RESET_DATA) != 0) {
        HalSerialBootMark("Boot: SDHCI softreset timeout\n");
        return -1;
    }
    SdhciDelayMicroseconds(2000);
    SdhciWrite8(SDHCI_POWER_CONTROL, (UINT8)(SDHCI_POWER_ON | SDHCI_POWER_330));
    SdhciDelayMicroseconds(10000);
    SdhciWrite8(SDHCI_HOST_CONTROL, 0); /* 1-bit */
    SdhciWrite8(SDHCI_TIMEOUT_CONTROL, 0x0E);
    /* 须 Enable 对应位，INT_STATUS 才会置位（SDHCI 规格） */
    SdhciWrite32(SDHCI_INT_ENABLE, 0xFFFFFFFFu);
    SdhciWrite32(SDHCI_SIGNAL_ENABLE, 0);

    gSdhciTwoTicksMicroseconds = ((2u * 1000000u) / IDENTIFY_HERTZ) + 1u;
    HalSerialBootMark("Boot: SDHCI clock id\n");
    if (SdhciClockConfigure(IDENTIFY_HERTZ) != 0) {
        HalSerialBootMark("Boot: SDHCI clock id fail\n");
        return -1;
    }
    SdhciWrite8(SDHCI_TIMEOUT_CONTROL, 0x0E);
    SdhciDelayMicroseconds(2000); /* ≥74 SDCLK @400kHz */

    Fl = SDHCI_CMD_RESP_NONE;
    (void)SdhciCommandSend(0, 0, Fl, 0);
    SdhciDelayMicroseconds(10000);

    /*
     * CMD8 = R7（CRC+INDEX）。失败不弃卡：按 SD Physical Layer 走 SDv1 ACMD41。
     * 录像曾 CMD8 fail → 整盘 RES → ttf miss。
     */
    HalSerialBootMark("Boot: SDHCI CMD8\n");
    Fl = (UINT16)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX);
    Cmd8Ok = 0;
    if (SdhciCommandSend(8, 0x1AAu, Fl, 0) == 0 && (gSdhciResponse[0] & 0xFFFu) == 0x1AAu) {
        Cmd8Ok = 1;
    } else {
        SdhciMarkHexadecimal32("Boot: SDHCI CMD8 fail st=", gSdhciLastCommandStatus);
        SdhciMarkHexadecimal32(" pst=", gSdhciLastPresentState);
        SdhciMarkHexadecimal32(" resp=", gSdhciResponse[0]);
        HalSerialBootMark(" → SDv1\n");
        SdhciSoftwareReset(SDHCI_RESET_CMD | SDHCI_RESET_DATA);
        (void)SdhciCommandSend(0, 0, SDHCI_CMD_RESP_NONE, 0);
        SdhciDelayMicroseconds(5000);
    }

    gSdhciHighCapacity = 0;
    /* ACMD41 应答 R3：无 CRC（MMC_RSP_R3）；带 CRC 会 INT_ERROR */
    AcmdArg = Cmd8Ok ? 0x40300000u : 0x00300000u;
    Ocr = 0;
    for (i = 0; i < 1000u; i++) {
        Fl = (UINT16)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX);
        if (SdhciCommandSend(55, 0, Fl, 0) != 0) {
            SdhciMarkHexadecimal32("Boot: SDHCI CMD55 fail st=", gSdhciLastCommandStatus);
            SdhciMarkHexadecimal32(" pst=", gSdhciLastPresentState);
            HalSerialBootMark("\n");
            return -1;
        }
        Fl = SDHCI_CMD_RESP_SHORT; /* R3 */
        if (SdhciCommandSend(41, AcmdArg, Fl, 0) != 0) {
            SdhciMarkHexadecimal32("Boot: SDHCI ACMD41 fail st=", gSdhciLastCommandStatus);
            SdhciMarkHexadecimal32(" pst=", gSdhciLastPresentState);
            HalSerialBootMark("\n");
            return -1;
        }
        Ocr = gSdhciResponse[0];
        if ((Ocr & 0x80000000u) != 0) {
            if ((Ocr & 0x40000000u) != 0) {
                gSdhciHighCapacity = 1;
            }
            break;
        }
        SdhciDelayMicroseconds(10000);
    }
    if ((Ocr & 0x80000000u) == 0) {
        HalSerialBootMark("Boot: SDHCI ACMD41 timeout\n");
        return -1;
    }

    Fl = (UINT16)(SDHCI_CMD_RESP_LONG | SDHCI_CMD_CRC);
    if (SdhciCommandSend(2, 0, Fl, 0) != 0) {
        HalSerialBootMark("Boot: SDHCI CMD2 fail\n");
        return -1;
    }
    Fl = (UINT16)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX);
    if (SdhciCommandSend(3, 0, Fl, 0) != 0) {
        HalSerialBootMark("Boot: SDHCI CMD3 fail\n");
        return -1;
    }
    gSdhciRelativeCardAddress = gSdhciResponse[0] & 0xFFFF0000u;

    Fl = (UINT16)(SDHCI_CMD_RESP_LONG | SDHCI_CMD_CRC);
    if (SdhciCommandSend(9, gSdhciRelativeCardAddress, Fl, 0) != 0) {
        HalSerialBootMark("Boot: SDHCI CMD9 fail\n");
        return -1;
    }
    Fl = (UINT16)(SDHCI_CMD_RESP_SHORT_BUSY | SDHCI_CMD_CRC | SDHCI_CMD_INDEX);
    if (SdhciCommandSend(7, gSdhciRelativeCardAddress, Fl, 0) != 0) {
        HalSerialBootMark("Boot: SDHCI CMD7 fail\n");
        return -1;
    }

    gSdhciTwoTicksMicroseconds = ((2u * 1000000u) / TRANSFER_HERTZ) + 1u;
    if (gSdhciTwoTicksMicroseconds < 2u) {
        gSdhciTwoTicksMicroseconds = 2u;
    }
    if (SdhciClockConfigure(TRANSFER_HERTZ) != 0) {
        HalSerialBootMark("Boot: SDHCI clock xfer fail\n");
        return -1;
    }

    Fl = (UINT16)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX);
    if (SdhciCommandSend(16, 512u, Fl, 0) != 0) {
        /* SDHC 可忽略 */
    }
    return 0;
}

int SdhciHostInitialize(void) {
    UINT32 Hertz;

    (void)BoardSdhciPowerOn();
    SdhciDelayMicroseconds(10000);
    gSdhciHostSpecification = (UINT16)(SdhciRead16(SDHCI_HOST_VERSION) & 0xFFu);
    {
        UINT32 Cap = SdhciRead32(SDHCI_CAPABILITIES);
        UINT32 CapMhz = (gSdhciHostSpecification >= SDHCI_SPEC_300)
                            ? ((Cap >> 8) & 0xFFu)
                            : ((Cap >> 8) & 0x3Fu);
        if (CapMhz != 0) {
            gSdhciBaseClockHertz = CapMhz * 1000000u;
        } else if (BoardMboxGetClockHz(BOARD_MBOX_CLOCK_EMMC, &Hertz) == 0 &&
                   Hertz != 0) {
            gSdhciBaseClockHertz = Hertz;
        } else {
            gSdhciBaseClockHertz = 100000000u;
            HalSerialBootMark("Boot: SDHCI clk fallback 100M\n");
        }
    }
    HalSerialBootMark("Boot: SDHCI ver=");
    {
        char Buffer[9];
        HalSerialFormatHex(Buffer, (UINT32)gSdhciHostSpecification, 2);
        HalSerialBootMark(Buffer);
        HalSerialBootMark(" base=");
        HalSerialFormatHex(Buffer, gSdhciBaseClockHertz, 8);
        HalSerialBootMark(Buffer);
    }
    HalSerialBootMark("Hertz\n");

    if ((SdhciRead32(SDHCI_PRESENT_STATE) & SDHCI_CARD_PRESENT) == 0) {
        /* 部分板 CD 位不可靠；仍试识别（U-Boot 亦不唯 CD） */
        HalSerialBootMark("Boot: SDHCI CD=0 try anyway\n");
    }
    return SdhciCardInitialize();
}

