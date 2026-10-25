/*
 * Sdhci.c — Pi3B+ Arasan SDHCI @ 0x3F300000 = WiFi SDIO（emmc_gpio34），非 µSD。
 *
 * µSD 走 Sdhost.c（0x3F202000 / sdhost_gpio48）。本文件保留作 WiFi/对照，
 * HalDevices 不再 Register；勿再当卡驱动试错。
 */
#include "Sdhci.h"
#include "Board.h"
#include "Block.h"
#include "Driver.h"
#include "DriverBlock.h"
#include "Hal.h"
#include "HalSerial.h"
#include "SerialLog.h"

#define ID_HZ        400000u
#define XFER_HZ      25000000u
#define SPIN_MAX     2000000u
#define WAIT_MS      100u

static UINT16 gHostSpec; /* SDHCI_SPEC_* */
static UINT32 gLastCmdSt;
static UINT32 gLastPresent;

static volatile UINT32 *Reg32(UINT32 Off) {
    return (volatile UINT32 *)(UINTN)(SDHCI_BASE + Off);
}

static UINT64 gCntFrq;
static UINT64 gLastWriteUs;
static UINT32 gTwoTicksUs = 6;
static UINT32 gBaseClkHz = 100000000u;
static UINT32 gRca;
static int gSdhc;
static int gReady;
static UINT32 gResp[4];

static UINT64 CntNow(void) {
    UINT64 V;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(V));
    return V;
}

static void DelayUs(UINT32 Us) {
    UINT64 Start;
    UINT64 Need;

    if (gCntFrq == 0) {
        __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(gCntFrq));
        if (gCntFrq == 0) {
            gCntFrq = 19200000ULL;
        }
    }
    Start = CntNow();
    Need = (gCntFrq * (UINT64)Us) / 1000000ULL;
    if (Need == 0) {
        Need = 1;
    }
    while ((CntNow() - Start) < Need) {
        HalCpuRelax();
    }
}

static UINT64 NowUs(void) {
    if (gCntFrq == 0) {
        DelayUs(0);
    }
    return (CntNow() * 1000000ULL) / gCntFrq;
}

/* Arasan：非 BUFFER 连续写须隔 ≥2 个 SD 时钟（U-Boot twoticks_delay） */
static void Wr32(UINT32 Off, UINT32 Val) {
    if (Off != SDHCI_BUFFER) {
        while ((NowUs() - gLastWriteUs) < (UINT64)gTwoTicksUs) {
            HalCpuRelax();
        }
    }
    *Reg32(Off) = Val;
    gLastWriteUs = NowUs();
}

static UINT32 Rd32(UINT32 Off) {
    /* 仅 4 字节对齐；外设窗非对齐 ldr 会挂（录像卡在 SoftReset） */
    return *Reg32(Off & ~3u);
}

static UINT8 Rd8(UINT32 Off) {
    UINT32 Shift = (Off & 3u) * 8u;
    return (UINT8)((Rd32(Off) >> Shift) & 0xFFu);
}

static UINT16 Rd16(UINT32 Off) {
    UINT32 Shift = (Off & 2u) ? 16u : 0u;
    return (UINT16)((Rd32(Off) >> Shift) & 0xFFFFu);
}

static void Wr8(UINT32 Off, UINT8 Val) {
    UINT32 Aligned = Off & ~3u;
    UINT32 Shift = (Off & 3u) * 8u;
    UINT32 Old = Rd32(Aligned);
    UINT32 Mask = 0xFFu << Shift;
    Wr32(Aligned, (Old & ~Mask) | (((UINT32)Val << Shift) & Mask));
}

/* 16 位寄存器：只改半字，勿 Wr32 钟控时清掉 timeout@0x2E */
static void Wr16(UINT32 Off, UINT16 Val) {
    UINT32 Aligned = Off & ~3u;
    UINT32 Shift = (Off & 2u) ? 16u : 0u;
    UINT32 Old = Rd32(Aligned);
    UINT32 Mask = 0xFFFFu << Shift;
    Wr32(Aligned, (Old & ~Mask) | (((UINT32)Val << Shift) & Mask));
}

/* 墙钟等待（SPIN_MAX 在 A53 上只有数 ms，CMD 易假超时 st=0） */
static int WaitMsMask32(UINT32 Off, UINT32 Mask, int Set, UINT32 Ms) {
    UINT32 n;
    for (n = 0; n < Ms * 10u; n++) {
        UINT32 V = Rd32(Off);
        if (Set) {
            if ((V & Mask) != 0) {
                return 0;
            }
        } else if ((V & Mask) == 0) {
            return 0;
        }
        DelayUs(100);
    }
    return -1;
}

static int WaitMsMask8(UINT32 Off, UINT8 Mask, int Set, UINT32 Ms) {
    UINT32 n;
    for (n = 0; n < Ms * 10u; n++) {
        UINT8 V = Rd8(Off);
        if (Set) {
            if ((V & Mask) != 0) {
                return 0;
            }
        } else if ((V & Mask) == 0) {
            return 0;
        }
        DelayUs(100);
    }
    return -1;
}

static void Wr16Pair(UINT32 OffLo, UINT16 Lo, UINT16 Hi) {
    /* TRANSFER_MODE@0x0C + COMMAND@0x0E 一次 32 位提交（bcm2835 shadow） */
    Wr32(OffLo & ~3u, ((UINT32)Hi << 16) | (UINT32)Lo);
}

static int SoftReset(UINT8 Mask) {
    Wr8(SDHCI_SOFTWARE_RESET, Mask);
    return WaitMsMask8(SDHCI_SOFTWARE_RESET, Mask, 0, WAIT_MS);
}

/* 对齐 U-Boot sdhci_set_clock（v2 幂次 / v3 偶除数） */
static int SetClock(UINT32 Hz) {
    UINT32 Div;
    UINT16 Clk;

    Wr16(SDHCI_CLOCK_CONTROL, 0);
    if (Hz == 0 || gBaseClkHz == 0) {
        return -1;
    }
    if (gHostSpec >= SDHCI_SPEC_300) {
        if (Hz >= gBaseClkHz) {
            Div = 1;
        } else {
            for (Div = 2; Div < SDHCI_MAX_DIV_300; Div += 2) {
                if ((gBaseClkHz / Div) <= Hz) {
                    break;
                }
            }
        }
        Div >>= 1;
    } else {
        for (Div = 1; Div < SDHCI_MAX_DIV_200; Div *= 2) {
            if ((gBaseClkHz / Div) <= Hz) {
                break;
            }
        }
        Div >>= 1;
    }

    Clk = (UINT16)(((Div & SDHCI_DIV_MASK) << SDHCI_DIVIDER_SHIFT) |
                   (((Div & SDHCI_DIV_HI_MASK) >> SDHCI_DIV_MASK_LEN)
                    << SDHCI_DIVIDER_HI_SHIFT) |
                   SDHCI_CLOCK_INT_EN);
    Wr16(SDHCI_CLOCK_CONTROL, Clk);
    if (WaitMsMask32(SDHCI_CLOCK_CONTROL, SDHCI_CLOCK_INT_STABLE, 1, 20) != 0) {
        return -1;
    }
    Wr16(SDHCI_CLOCK_CONTROL, (UINT16)(Clk | SDHCI_CLOCK_CARD_EN));
    DelayUs(1000);
    return 0;
}

static int SendCmd(UINT8 Cmd, UINT32 Arg, UINT16 Flags, UINT16 Xfer) {
    UINT32 St;
    UINT32 n;

    if (WaitMsMask32(SDHCI_PRESENT_STATE, SDHCI_CMD_INHIBIT | SDHCI_DATA_INHIBIT, 0,
                     WAIT_MS) != 0) {
        gLastPresent = Rd32(SDHCI_PRESENT_STATE);
        gLastCmdSt = Rd32(SDHCI_INT_STATUS);
        return -1;
    }
    Wr32(SDHCI_INT_STATUS, 0xFFFFFFFFu);
    Wr32(SDHCI_ARGUMENT, Arg);
    Wr16Pair(SDHCI_TRANSFER_MODE, Xfer, (UINT16)SDHCI_MAKE_CMD(Cmd, Flags));

    for (n = 0; n < WAIT_MS * 10u; n++) {
        St = Rd32(SDHCI_INT_STATUS);
        if ((St & SDHCI_INT_ERROR) != 0) {
            gLastCmdSt = St;
            gLastPresent = Rd32(SDHCI_PRESENT_STATE);
            Wr32(SDHCI_INT_STATUS, St);
            SoftReset(SDHCI_RESET_CMD | SDHCI_RESET_DATA);
            return -1;
        }
        if ((St & SDHCI_INT_RESPONSE) != 0) {
            break;
        }
        DelayUs(100);
    }
    if (n >= WAIT_MS * 10u) {
        gLastCmdSt = Rd32(SDHCI_INT_STATUS);
        gLastPresent = Rd32(SDHCI_PRESENT_STATE);
        SoftReset(SDHCI_RESET_CMD | SDHCI_RESET_DATA);
        return -1;
    }
    gResp[0] = Rd32(SDHCI_RESPONSE + 0);
    gResp[1] = Rd32(SDHCI_RESPONSE + 4);
    gResp[2] = Rd32(SDHCI_RESPONSE + 8);
    gResp[3] = Rd32(SDHCI_RESPONSE + 12);
    Wr32(SDHCI_INT_STATUS, SDHCI_INT_RESPONSE);
    return 0;
}

static int PioReadBlock(UINT8 *Buf) {
    UINT32 i;
    UINT32 St;

    for (i = 0; i < 128u; i++) {
        UINT32 Spin;
        for (Spin = 0; Spin < SPIN_MAX; Spin++) {
            St = Rd32(SDHCI_INT_STATUS);
            if ((St & SDHCI_INT_ERROR) != 0) {
                Wr32(SDHCI_INT_STATUS, St);
                return -1;
            }
            if ((Rd32(SDHCI_PRESENT_STATE) & SDHCI_DATA_AVAILABLE) != 0) {
                break;
            }
            HalCpuRelax();
        }
        if (Spin >= SPIN_MAX) {
            return -1;
        }
        {
            UINT32 W = Rd32(SDHCI_BUFFER);
            Buf[i * 4u + 0] = (UINT8)(W);
            Buf[i * 4u + 1] = (UINT8)(W >> 8);
            Buf[i * 4u + 2] = (UINT8)(W >> 16);
            Buf[i * 4u + 3] = (UINT8)(W >> 24);
        }
    }
    for (i = 0; i < SPIN_MAX; i++) {
        St = Rd32(SDHCI_INT_STATUS);
        if ((St & SDHCI_INT_ERROR) != 0) {
            Wr32(SDHCI_INT_STATUS, St);
            return -1;
        }
        if ((St & SDHCI_INT_DATA_END) != 0) {
            Wr32(SDHCI_INT_STATUS, SDHCI_INT_DATA_END | SDHCI_INT_DATA_AVAIL);
            return 0;
        }
        HalCpuRelax();
    }
    return -1;
}

static int ReadOne(UINT32 Lba, UINT8 *Buf) {
    UINT32 Arg = gSdhc ? Lba : (Lba * 512u);
    UINT16 Flags = (UINT16)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX |
                            SDHCI_CMD_DATA);
    UINT16 Xfer = (UINT16)(SDHCI_TRNS_READ | SDHCI_TRNS_BLK_CNT_EN);

    /* 0x04 BLOCK_SIZE + 0x06 BLOCK_COUNT 同 32 位字 */
    Wr32(SDHCI_BLOCK_SIZE, 512u | (1u << 16));

    if (SendCmd(17, Arg, Flags, Xfer) != 0) {
        return -1;
    }
    return PioReadBlock(Buf);
}

static void MarkHex32(const char *Tag, UINT32 V) {
    char Buf[9];
    HalSerialBootMark(Tag);
    HalSerialFormatHex(Buf, V, 8);
    HalSerialBootMark(Buf);
}

static int CardInit(void) {
    UINT32 Ocr;
    UINT32 i;
    UINT16 Fl;
    int Cmd8Ok;
    UINT32 AcmdArg;

    /*
     * 录像：CMD8/55 超时 st=INT_TIMEOUT、pst 有卡。
     * SoftReset ALL 会拆掉 U-Boot 已训好的总线；只清 CMD/DATA，保留电源/钟树。
     */
    HalSerialBootMark("Boot: SDHCI reset cmd|data\n");
    if (SoftReset(SDHCI_RESET_CMD | SDHCI_RESET_DATA) != 0) {
        HalSerialBootMark("Boot: SDHCI softreset timeout\n");
        return -1;
    }
    DelayUs(2000);
    Wr8(SDHCI_POWER_CONTROL, (UINT8)(SDHCI_POWER_ON | SDHCI_POWER_330));
    DelayUs(10000);
    Wr8(SDHCI_HOST_CONTROL, 0); /* 1-bit */
    Wr8(SDHCI_TIMEOUT_CONTROL, 0x0E);
    /* 须 Enable 对应位，INT_STATUS 才会置位（SDHCI 规格） */
    Wr32(SDHCI_INT_ENABLE, 0xFFFFFFFFu);
    Wr32(SDHCI_SIGNAL_ENABLE, 0);

    gTwoTicksUs = ((2u * 1000000u) / ID_HZ) + 1u;
    HalSerialBootMark("Boot: SDHCI clock id\n");
    if (SetClock(ID_HZ) != 0) {
        HalSerialBootMark("Boot: SDHCI clock id fail\n");
        return -1;
    }
    Wr8(SDHCI_TIMEOUT_CONTROL, 0x0E);
    DelayUs(2000); /* ≥74 SDCLK @400kHz */

    Fl = SDHCI_CMD_RESP_NONE;
    (void)SendCmd(0, 0, Fl, 0);
    DelayUs(10000);

    /*
     * CMD8 = R7（CRC+INDEX）。失败不弃卡：按 SD Physical Layer 走 SDv1 ACMD41。
     * 录像曾 CMD8 fail → 整盘 RES → ttf miss。
     */
    HalSerialBootMark("Boot: SDHCI CMD8\n");
    Fl = (UINT16)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX);
    Cmd8Ok = 0;
    if (SendCmd(8, 0x1AAu, Fl, 0) == 0 && (gResp[0] & 0xFFFu) == 0x1AAu) {
        Cmd8Ok = 1;
    } else {
        MarkHex32("Boot: SDHCI CMD8 fail st=", gLastCmdSt);
        MarkHex32(" pst=", gLastPresent);
        MarkHex32(" resp=", gResp[0]);
        HalSerialBootMark(" → SDv1\n");
        SoftReset(SDHCI_RESET_CMD | SDHCI_RESET_DATA);
        (void)SendCmd(0, 0, SDHCI_CMD_RESP_NONE, 0);
        DelayUs(5000);
    }

    gSdhc = 0;
    /* ACMD41 应答 R3：无 CRC（MMC_RSP_R3）；带 CRC 会 INT_ERROR */
    AcmdArg = Cmd8Ok ? 0x40300000u : 0x00300000u;
    Ocr = 0;
    for (i = 0; i < 1000u; i++) {
        Fl = (UINT16)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX);
        if (SendCmd(55, 0, Fl, 0) != 0) {
            MarkHex32("Boot: SDHCI CMD55 fail st=", gLastCmdSt);
            MarkHex32(" pst=", gLastPresent);
            HalSerialBootMark("\n");
            return -1;
        }
        Fl = SDHCI_CMD_RESP_SHORT; /* R3 */
        if (SendCmd(41, AcmdArg, Fl, 0) != 0) {
            MarkHex32("Boot: SDHCI ACMD41 fail st=", gLastCmdSt);
            MarkHex32(" pst=", gLastPresent);
            HalSerialBootMark("\n");
            return -1;
        }
        Ocr = gResp[0];
        if ((Ocr & 0x80000000u) != 0) {
            if ((Ocr & 0x40000000u) != 0) {
                gSdhc = 1;
            }
            break;
        }
        DelayUs(10000);
    }
    if ((Ocr & 0x80000000u) == 0) {
        HalSerialBootMark("Boot: SDHCI ACMD41 timeout\n");
        return -1;
    }

    Fl = (UINT16)(SDHCI_CMD_RESP_LONG | SDHCI_CMD_CRC);
    if (SendCmd(2, 0, Fl, 0) != 0) {
        HalSerialBootMark("Boot: SDHCI CMD2 fail\n");
        return -1;
    }
    Fl = (UINT16)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX);
    if (SendCmd(3, 0, Fl, 0) != 0) {
        HalSerialBootMark("Boot: SDHCI CMD3 fail\n");
        return -1;
    }
    gRca = gResp[0] & 0xFFFF0000u;

    Fl = (UINT16)(SDHCI_CMD_RESP_LONG | SDHCI_CMD_CRC);
    if (SendCmd(9, gRca, Fl, 0) != 0) {
        HalSerialBootMark("Boot: SDHCI CMD9 fail\n");
        return -1;
    }
    Fl = (UINT16)(SDHCI_CMD_RESP_SHORT_BUSY | SDHCI_CMD_CRC | SDHCI_CMD_INDEX);
    if (SendCmd(7, gRca, Fl, 0) != 0) {
        HalSerialBootMark("Boot: SDHCI CMD7 fail\n");
        return -1;
    }

    gTwoTicksUs = ((2u * 1000000u) / XFER_HZ) + 1u;
    if (gTwoTicksUs < 2u) {
        gTwoTicksUs = 2u;
    }
    if (SetClock(XFER_HZ) != 0) {
        HalSerialBootMark("Boot: SDHCI clock xfer fail\n");
        return -1;
    }

    Fl = (UINT16)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX);
    if (SendCmd(16, 512u, Fl, 0) != 0) {
        /* SDHC 可忽略 */
    }
    return 0;
}

static int HostInit(void) {
    UINT32 Hz;

    (void)BoardSdhciPowerOn();
    DelayUs(10000);
    gHostSpec = (UINT16)(Rd16(SDHCI_HOST_VERSION) & 0xFFu);
    {
        UINT32 Cap = Rd32(SDHCI_CAPABILITIES);
        UINT32 CapMhz = (gHostSpec >= SDHCI_SPEC_300)
                            ? ((Cap >> 8) & 0xFFu)
                            : ((Cap >> 8) & 0x3Fu);
        if (CapMhz != 0) {
            gBaseClkHz = CapMhz * 1000000u;
        } else if (BoardMboxGetClockHz(BOARD_MBOX_CLOCK_EMMC, &Hz) == 0 &&
                   Hz != 0) {
            gBaseClkHz = Hz;
        } else {
            gBaseClkHz = 100000000u;
            HalSerialBootMark("Boot: SDHCI clk fallback 100M\n");
        }
    }
    HalSerialBootMark("Boot: SDHCI ver=");
    {
        char Buf[9];
        HalSerialFormatHex(Buf, (UINT32)gHostSpec, 2);
        HalSerialBootMark(Buf);
        HalSerialBootMark(" base=");
        HalSerialFormatHex(Buf, gBaseClkHz, 8);
        HalSerialBootMark(Buf);
    }
    HalSerialBootMark("Hz\n");

    if ((Rd32(SDHCI_PRESENT_STATE) & SDHCI_CARD_PRESENT) == 0) {
        /* 部分板 CD 位不可靠；仍试识别（U-Boot 亦不唯 CD） */
        HalSerialBootMark("Boot: SDHCI CD=0 try anyway\n");
    }
    return CardInit();
}

static int SdhciProbe(UINT32 Drive) {
    return (Drive == 0 && gReady) ? 1 : 0;
}

static int SdhciRead(UINT32 Drive, UINT32 Lba, UINT32 Count, void *Buffer) {
    UINT8 *P = (UINT8 *)Buffer;
    UINT32 i;

    (void)Drive;
    if (!gReady || !Buffer || Count == 0) {
        return 0;
    }
    for (i = 0; i < Count; i++) {
        if (ReadOne(Lba + i, P + (UINTN)i * 512u) != 0) {
            SoftReset(SDHCI_RESET_CMD | SDHCI_RESET_DATA);
            return 0;
        }
    }
    return 1;
}

static int SdhciWrite(UINT32 Drive, UINT32 Lba, UINT32 Count, const void *Buffer) {
    (void)Drive;
    (void)Lba;
    (void)Count;
    (void)Buffer;
    return 0; /* 本刀只读 */
}

static const BLOCK_BACKEND gSdhciBackend = {
    .Probe = SdhciProbe,
    .ReadSectors = SdhciRead,
    .WriteSectors = SdhciWrite,
    .Flush = 0,
};

static int SdhciDriverProbe(const DRIVER *Self, void *BusCtx, void **OutPrivate) {
    (void)Self;
    (void)BusCtx;
    if (gReady) {
        if (OutPrivate) {
            *OutPrivate = 0;
        }
        return 0;
    }
    if (HostInit() != 0) {
        return -1;
    }
    gReady = 1;
    HalSerialBootMark(gSdhc ? "Boot: SDHCI SDHC ready\n" : "Boot: SDHCI SDSC ready\n");
    if (OutPrivate) {
        *OutPrivate = 0;
    }
    return 0;
}

static int SdhciDriverBind(DRIVER_INSTANCE *Inst) {
    (void)Inst;
    return DriverBlockAttach(&gSdhciBackend);
}

static void SdhciDriverRemove(DRIVER_INSTANCE *Inst) {
    (void)Inst;
    gReady = 0;
}

static const DRIVER gSdhciDriver = {
    .Name = "bcm-sdhci",
    .Class = DRIVER_CLASS_BLOCK,
    .Match = 0,
    .Probe = SdhciDriverProbe,
    .Bind = SdhciDriverBind,
    .Remove = SdhciDriverRemove,
};

void BoardSdhciRegister(void) {
    /* 故意不挂：µSD 用 BoardSdhostRegister；本驱动留档勿再 Probe 读卡 */
    (void)gSdhciDriver;
    HalSerialBootMark("Boot: SDHCI skipped (WiFi SDIO, not µSD)\n");
}
