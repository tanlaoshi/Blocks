/*
 * Card.c — SDHOST 卡/主机初始化（bd-pi-sd）
 */
#include "Sdhost/Private.h"

int SdhostCardInitialize(void) {
    UINT32 Ocr;
    UINT32 i;
    int Cmd8Ok;
    UINT32 AcmdArg;

    HalSerialBootMark("Boot: SDHOST reset\n");
    gSdhostHostConfiguration = SDHCFG_BUSY_IRPT_EN;
    gSdhostClockDivider = 0;
    SdhostResetController();
    SdhostInterfaceConfigure(IDENTIFY_HERTZ, 0);
    SdhostDelayMicroseconds(2000);

    (void)SdhostCommandSend(0, 0, RESPONSE_NONE, 0, 0);
    SdhostDelayMicroseconds(10000);

    HalSerialBootMark("Boot: SDHOST CMD8\n");
    Cmd8Ok = 0;
    if (SdhostCommandSend(8, 0x1AAu, RESPONSE_R1, 0, 0) == 0 &&
        (gSdhostResponse[0] & 0xFFFu) == 0x1AAu) {
        Cmd8Ok = 1;
    } else {
        SdhostMarkHexadecimal32("Boot: SDHOST CMD8 fail hsts=", gSdhostLastHostStatus);
        HalSerialBootMark(" → SDv1\n");
        (void)SdhostCommandSend(0, 0, RESPONSE_NONE, 0, 0);
        SdhostDelayMicroseconds(5000);
    }

    gSdhostHighCapacity = 0;
    AcmdArg = Cmd8Ok ? 0x40300000u : 0x00300000u;
    Ocr = 0;
    for (i = 0; i < 1000u; i++) {
        if (SdhostCommandSend(55, 0, RESPONSE_R1, 0, 0) != 0) {
            SdhostMarkHexadecimal32("Boot: SDHOST CMD55 fail hsts=", gSdhostLastHostStatus);
            HalSerialBootMark("\n");
            return -1;
        }
        if (SdhostCommandSend(41, AcmdArg, RESPONSE_R3, 0, 0) != 0) {
            SdhostMarkHexadecimal32("Boot: SDHOST ACMD41 fail hsts=", gSdhostLastHostStatus);
            HalSerialBootMark("\n");
            return -1;
        }
        Ocr = gSdhostResponse[0];
        if ((Ocr & 0x80000000u) != 0) {
            if ((Ocr & 0x40000000u) != 0) {
                gSdhostHighCapacity = 1;
            }
            break;
        }
        SdhostDelayMicroseconds(10000);
    }
    if ((Ocr & 0x80000000u) == 0) {
        HalSerialBootMark("Boot: SDHOST ACMD41 timeout\n");
        return -1;
    }

    if (SdhostCommandSend(2, 0, RESPONSE_R2, 0, 0) != 0) {
        HalSerialBootMark("Boot: SDHOST CMD2 fail\n");
        return -1;
    }
    if (SdhostCommandSend(3, 0, RESPONSE_R1, 0, 0) != 0) {
        HalSerialBootMark("Boot: SDHOST CMD3 fail\n");
        return -1;
    }
    gSdhostRelativeCardAddress = gSdhostResponse[0] & 0xFFFF0000u;

    if (SdhostCommandSend(9, gSdhostRelativeCardAddress, RESPONSE_R2, 0, 0) != 0) {
        HalSerialBootMark("Boot: SDHOST CMD9 fail\n");
        return -1;
    }
    if (SdhostCommandSend(7, gSdhostRelativeCardAddress, RESPONSE_R1B, 0, 0) != 0) {
        HalSerialBootMark("Boot: SDHOST CMD7 fail\n");
        return -1;
    }

    /* 1-bit 先过读（计划：4-bit 可选） */
    SdhostInterfaceConfigure(TRANSFER_HERTZ, 0);

    if (SdhostCommandSend(16, 512u, RESPONSE_R1, 0, 0) != 0) {
        /* SDHC 可忽略 */
    }
    return 0;
}

int SdhostHostInitialize(void) {
    UINT32 Hertz;

    /* U-Boot get_mmc_clock 亦先 power SDHCI 域；跟同源 */
    (void)BoardSdhciPowerOn();
    SdhostDelayMicroseconds(10000);

    if (BoardMboxGetClockHz(BOARD_MBOX_CLOCK_CORE, &Hertz) == 0 && Hertz != 0) {
        gSdhostMaximumClock = Hertz;
    } else {
        gSdhostMaximumClock = 250000000u;
        HalSerialBootMark("Boot: SDHOST clk fallback 250M\n");
    }
    HalSerialBootMark("Boot: SDHOST @3F202000 core=");
    {
        char Buffer[9];
        HalSerialFormatHex(Buffer, gSdhostMaximumClock, 8);
        HalSerialBootMark(Buffer);
    }
    HalSerialBootMark("Hertz\n");

    return SdhostCardInitialize();
}

int SdhostReadOneBlock(UINT32 LogicalBlockAddress, UINT8 *Buffer) {
    UINT32 Argument =
        gSdhostHighCapacity ? LogicalBlockAddress : (LogicalBlockAddress * 512u);
    return SdhostCommandSend(17, Argument, RESPONSE_R1, Buffer, 0);
}

int SdhostWriteOneBlock(UINT32 LogicalBlockAddress, const UINT8 *Buffer) {
    UINT32 Argument =
        gSdhostHighCapacity ? LogicalBlockAddress : (LogicalBlockAddress * 512u);
    /* CMD24；R1 后 PIO 写，再等 FSM（对表 Duo 语义 / Linux bcm2835_sdhost） */
    return SdhostCommandSend(24, Argument, RESPONSE_R1, (UINT8 *)(UINTN)Buffer, 1);
}

