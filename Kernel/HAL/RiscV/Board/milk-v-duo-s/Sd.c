/*
 * Sd.c — Duo S µSD：SDHCI PIO 只读 → BLOCK_BACKEND
 *
 * 主参考：SD Host Controller 简化规格；
 * 厂商复位：Linux 6.12 sdhci-of-dwcmshc.c cv18xx_sdhci_reset（非 eMMC 路径）。
 * 基钟：U-Boot drivers/mmc/cv1800b_sdhci.c MMC_MAX_CLOCK。
 * CAPABILITIES 里的基钟不可用（SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN）。
 * 只读：WriteSectors 失败。不发擦除/写命令，不动 FSBL / U-Boot。
 */
#include "Sd.h"
#include "Board.h"
#include "Block.h"
#include "Driver.h"
#include "DriverBlock.h"
#include "Hal.h"
#include "HalSerial.h"

#define ID_HZ   400000u
#define XFER_HZ 25000000u
/* cv1800b_sdhci.c：CAP 基钟坏掉，分频按这个上限 */
#define SD_BASE_CLK_HZ 375000000u
#define AREA1_MASK 0xFFFu

static UINT16 gHostSpec;
static UINT32 gVendor;
static UINT32 gRca;
static UINT32 gResp[4];
static UINT32 gLastSt;
static UINT32 gLastPresent;
static int gSdhc;
static int gReady;

static volatile UINT32 *Reg32(UINT32 Off) {
    return (volatile UINT32 *)(UINTN)(SDHCI_BASE + Off);
}

static UINT64 RdTime(void) {
    UINT64 V;
    __asm__ volatile("rdtime %0" : "=r"(V));
    return V;
}

static void DelayUs(UINT32 Us) {
    UINT64 Start = RdTime();
    UINT64 Need = ((UINT64)BOARD_TIMEBASE_HZ * (UINT64)Us) / 1000000ULL;
    if (Need == 0) {
        Need = 1;
    }
    while ((RdTime() - Start) < Need) {
        HalCpuRelax();
    }
}

static UINT32 Rd32(UINT32 Off) {
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

static void Wr32(UINT32 Off, UINT32 Val) {
    *Reg32(Off & ~3u) = Val;
}

static void Wr8(UINT32 Off, UINT8 Val) {
    UINT32 Aligned = Off & ~3u;
    UINT32 Shift = (Off & 3u) * 8u;
    UINT32 Old = Rd32(Aligned);
    UINT32 Mask = 0xFFu << Shift;
    Wr32(Aligned, (Old & ~Mask) | ((UINT32)Val << Shift));
}

static void Wr16(UINT32 Off, UINT16 Val) {
    UINT32 Aligned = Off & ~3u;
    UINT32 Shift = (Off & 2u) ? 16u : 0u;
    UINT32 Old = Rd32(Aligned);
    UINT32 Mask = 0xFFFFu << Shift;
    Wr32(Aligned, (Old & ~Mask) | ((UINT32)Val << Shift));
}

static void Mark(const char *Text) {
    HalSerialBootMark(Text);
}

static void MarkHex(const char *Tag, UINT32 V) {
    char Buf[9];
    HalSerialBootMark(Tag);
    HalSerialFormatHex(Buf, V, 8);
    HalSerialBootMark(Buf);
}

static int WaitMs8(UINT32 Off, UINT8 Mask, int Set, UINT32 Ms) {
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

static int WaitMs32(UINT32 Off, UINT32 Mask, int Set, UINT32 Ms) {
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

static int PhyAfterReset(void);

static int SoftReset(UINT8 Mask) {
    Wr8(SDHCI_SOFTWARE_RESET, Mask);
    if (WaitMs8(SDHCI_SOFTWARE_RESET, Mask, 0, 100) != 0) {
        return -1;
    }
    /* Linux cv18xx_sdhci_reset：每次复位后重写 latency / PHY bypass */
    return PhyAfterReset();
}

/* Linux cv18xx_sdhci_reset：SD 槽不置 EMMC_FUNC_EN */
static int PhyAfterReset(void) {
    UINT32 Area = Rd32(SDHCI_P_VENDOR_AREA1) & AREA1_MASK;
    UINT32 Ctrl;
    UINT32 Cfg;

    if (Area < 0x100u || Area + CV_PHY_CONFIG >= 0x1000u) {
        MarkHex("Boot: SD vendor area bad ", Area);
        Mark("\n");
        return -1;
    }
    gVendor = Area;
    Ctrl = Rd32(Area + CV_MSHC_CTRL);
    Wr32(Area + CV_MSHC_CTRL, Ctrl | CV_LATENCY_1T);
    Cfg = Rd32(Area + CV_PHY_CONFIG);
    Wr32(Area + CV_PHY_CONFIG, Cfg | CV_PHY_TX_BPS);
    Wr32(Area + CV_PHY_TX_RX, CV_PHY_DLY_BYPASS);
    return 0;
}

static int SetClock(UINT32 Hz) {
    UINT32 Div;
    UINT16 Clk;

    Wr16(SDHCI_CLOCK_CONTROL, 0);
    if (Hz == 0) {
        return -1;
    }
    if (gHostSpec >= SDHCI_SPEC_300) {
        if (Hz >= SD_BASE_CLK_HZ) {
            Div = 1;
        } else {
            for (Div = 2; Div < SDHCI_MAX_DIV_300; Div += 2) {
                if ((SD_BASE_CLK_HZ / Div) <= Hz) {
                    break;
                }
            }
        }
        Div >>= 1;
    } else {
        for (Div = 1; Div < 256u; Div *= 2) {
            if ((SD_BASE_CLK_HZ / Div) <= Hz) {
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
    if (WaitMs32(SDHCI_CLOCK_CONTROL, SDHCI_CLOCK_INT_STABLE, 1, 100) != 0) {
        return -1;
    }
    Wr16(SDHCI_CLOCK_CONTROL, (UINT16)(Clk | SDHCI_CLOCK_CARD_EN));
    Wr8(SDHCI_TIMEOUT_CONTROL, 0x0E);
    DelayUs(1000);
    return 0;
}

static int SendCmd(UINT8 Cmd, UINT32 Arg, UINT16 Flags, UINT16 Xfer) {
    UINT32 Inhibit = SDHCI_CMD_INHIBIT;
    UINT32 n;
    UINT32 St;

    if ((Flags & SDHCI_CMD_DATA) != 0) {
        Inhibit |= SDHCI_DATA_INHIBIT;
    }
    if (WaitMs32(SDHCI_PRESENT_STATE, Inhibit, 0, 100) != 0) {
        gLastPresent = Rd32(SDHCI_PRESENT_STATE);
        gLastSt = Rd32(SDHCI_INT_STATUS);
        return -1;
    }
    Wr32(SDHCI_INT_STATUS, 0xFFFFFFFFu);
    Wr32(SDHCI_ARGUMENT, Arg);
    Wr32(SDHCI_TRANSFER_MODE, ((UINT32)SDHCI_MAKE_CMD(Cmd, Flags) << 16) | Xfer);

    for (n = 0; n < 1000u; n++) {
        St = Rd32(SDHCI_INT_STATUS);
        if ((St & SDHCI_INT_ERROR) != 0) {
            gLastSt = St;
            gLastPresent = Rd32(SDHCI_PRESENT_STATE);
            Wr32(SDHCI_INT_STATUS, St);
            (void)SoftReset(SDHCI_RESET_CMD | SDHCI_RESET_DATA);
            return -1;
        }
        if ((St & SDHCI_INT_RESPONSE) != 0) {
            break;
        }
        DelayUs(100);
    }
    if (n >= 1000u) {
        gLastSt = Rd32(SDHCI_INT_STATUS);
        gLastPresent = Rd32(SDHCI_PRESENT_STATE);
        (void)SoftReset(SDHCI_RESET_CMD | SDHCI_RESET_DATA);
        return -1;
    }
    gResp[0] = Rd32(SDHCI_RESPONSE);
    gResp[1] = Rd32(SDHCI_RESPONSE + 4);
    gResp[2] = Rd32(SDHCI_RESPONSE + 8);
    gResp[3] = Rd32(SDHCI_RESPONSE + 12);
    Wr32(SDHCI_INT_STATUS, SDHCI_INT_RESPONSE);
    if ((Flags & 0x03u) == SDHCI_CMD_RESP_SHORT_BUSY) {
        if (WaitMs32(SDHCI_INT_STATUS, SDHCI_INT_DATA_END | SDHCI_INT_ERROR, 1,
                     1000) != 0) {
            gLastSt = Rd32(SDHCI_INT_STATUS);
            return -1;
        }
        if ((Rd32(SDHCI_INT_STATUS) & SDHCI_INT_ERROR) != 0) {
            gLastSt = Rd32(SDHCI_INT_STATUS);
            Wr32(SDHCI_INT_STATUS, gLastSt);
            (void)SoftReset(SDHCI_RESET_CMD | SDHCI_RESET_DATA);
            return -1;
        }
        Wr32(SDHCI_INT_STATUS, SDHCI_INT_DATA_END);
    }
    return 0;
}

static int PioRead(UINT8 *Buf) {
    UINT32 i;

    if (WaitMs32(SDHCI_INT_STATUS, SDHCI_INT_DATA_AVAIL | SDHCI_INT_ERROR, 1,
                 500) != 0) {
        return -1;
    }
    if ((Rd32(SDHCI_INT_STATUS) & SDHCI_INT_ERROR) != 0) {
        gLastSt = Rd32(SDHCI_INT_STATUS);
        Wr32(SDHCI_INT_STATUS, gLastSt);
        return -1;
    }
    Wr32(SDHCI_INT_STATUS, SDHCI_INT_DATA_AVAIL);
    for (i = 0; i < 128u; i++) {
        UINT32 W = Rd32(SDHCI_BUFFER);
        Buf[i * 4u + 0] = (UINT8)W;
        Buf[i * 4u + 1] = (UINT8)(W >> 8);
        Buf[i * 4u + 2] = (UINT8)(W >> 16);
        Buf[i * 4u + 3] = (UINT8)(W >> 24);
    }
    if (WaitMs32(SDHCI_INT_STATUS, SDHCI_INT_DATA_END | SDHCI_INT_ERROR, 1,
                 500) != 0) {
        return -1;
    }
    if ((Rd32(SDHCI_INT_STATUS) & SDHCI_INT_ERROR) != 0) {
        gLastSt = Rd32(SDHCI_INT_STATUS);
        Wr32(SDHCI_INT_STATUS, gLastSt);
        return -1;
    }
    Wr32(SDHCI_INT_STATUS, SDHCI_INT_DATA_END);
    return 0;
}

static int ReadOne(UINT32 Lba, UINT8 *Buf) {
    UINT32 Arg = gSdhc ? Lba : (Lba * 512u);
    UINT16 Flags = (UINT16)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX |
                            SDHCI_CMD_DATA);
    UINT16 Xfer = (UINT16)(SDHCI_TRNS_READ | SDHCI_TRNS_BLK_CNT_EN);

    Wr32(SDHCI_BLOCK_SIZE, 512u | (1u << 16));
    if (SendCmd(17, Arg, Flags, Xfer) != 0) {
        return -1;
    }
    return PioRead(Buf);
}

static void Fail(const char *What) {
    Mark(What);
    MarkHex(" st=", gLastSt);
    MarkHex(" pst=", gLastPresent);
    Mark("\n");
}

static int CardInit(void) {
    UINT16 Fl;
    UINT32 Ocr;
    UINT32 i;
    int Cmd8Ok;
    UINT32 Acmd;

    Mark("Boot: SD reset\n");
    if (SoftReset(SDHCI_RESET_ALL) != 0) {
        Mark("Boot: SD softreset timeout\n");
        return -1;
    }
    MarkHex("Boot: SD vendor ", gVendor);
    Mark("\n");
    Wr8(SDHCI_POWER_CONTROL, (UINT8)(SDHCI_POWER_ON | SDHCI_POWER_330));
    DelayUs(10000);
    Wr8(SDHCI_HOST_CONTROL, 0);
    Wr8(SDHCI_TIMEOUT_CONTROL, 0x0E);
    Wr32(SDHCI_INT_ENABLE, 0xFFFFFFFFu);
    Wr32(SDHCI_SIGNAL_ENABLE, 0);
    /* no-1-8-v：保持 3.3V，不写 HOST_CONTROL2 的 1.8V 位 */

    if (SetClock(ID_HZ) != 0) {
        Mark("Boot: SD clock id fail\n");
        return -1;
    }
    DelayUs(2000);

    if (WaitMs32(SDHCI_PRESENT_STATE, SDHCI_CARD_STABLE, 1, 100) != 0 ||
        (Rd32(SDHCI_PRESENT_STATE) & SDHCI_CARD_INSERTED) == 0) {
        MarkHex("Boot: SD no card pst=", Rd32(SDHCI_PRESENT_STATE));
        Mark("\n");
        return -1;
    }

    (void)SendCmd(0, 0, SDHCI_CMD_RESP_NONE, 0);
    DelayUs(10000);

    Fl = (UINT16)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX);
    Cmd8Ok = 0;
    if (SendCmd(8, 0x1AAu, Fl, 0) == 0 && (gResp[0] & 0xFFFu) == 0x1AAu) {
        Cmd8Ok = 1;
    } else {
        Fail("Boot: SD CMD8 fail");
        (void)SoftReset(SDHCI_RESET_CMD | SDHCI_RESET_DATA);
        (void)SendCmd(0, 0, SDHCI_CMD_RESP_NONE, 0);
        DelayUs(5000);
    }

    gSdhc = 0;
    Acmd = Cmd8Ok ? 0x40300000u : 0x00300000u;
    Ocr = 0;
    for (i = 0; i < 100u; i++) {
        Fl = (UINT16)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX);
        if (SendCmd(55, 0, Fl, 0) != 0) {
            Fail("Boot: SD CMD55 fail");
            return -1;
        }
        if (SendCmd(41, Acmd, SDHCI_CMD_RESP_SHORT, 0) != 0) {
            Fail("Boot: SD ACMD41 fail");
            return -1;
        }
        Ocr = gResp[0];
        if ((Ocr & 0x80000000u) != 0) {
            gSdhc = (Ocr & 0x40000000u) ? 1 : 0;
            break;
        }
        DelayUs(10000);
    }
    if ((Ocr & 0x80000000u) == 0) {
        Mark("Boot: SD ACMD41 timeout\n");
        return -1;
    }

    if (SetClock(XFER_HZ) != 0) {
        Mark("Boot: SD clock xfer fail\n");
        return -1;
    }

    Fl = (UINT16)(SDHCI_CMD_RESP_LONG | SDHCI_CMD_CRC);
    if (SendCmd(2, 0, Fl, 0) != 0) {
        Fail("Boot: SD CMD2 fail");
        return -1;
    }
    Fl = (UINT16)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX);
    if (SendCmd(3, 0, Fl, 0) != 0) {
        Fail("Boot: SD CMD3 fail");
        return -1;
    }
    gRca = gResp[0] & 0xFFFF0000u;

    Fl = (UINT16)(SDHCI_CMD_RESP_SHORT_BUSY | SDHCI_CMD_CRC | SDHCI_CMD_INDEX);
    if (SendCmd(7, gRca, Fl, 0) != 0) {
        Fail("Boot: SD CMD7 fail");
        return -1;
    }

    Fl = (UINT16)(SDHCI_CMD_RESP_SHORT | SDHCI_CMD_CRC | SDHCI_CMD_INDEX);
    if (SendCmd(55, gRca, Fl, 0) == 0 && SendCmd(6, 2u, Fl, 0) == 0) {
        Wr8(SDHCI_HOST_CONTROL, SDHCI_WIDTH_4BIT);
    } else {
        Mark("Boot: SD stay 1-bit\n");
    }

    (void)SendCmd(16, 512u, Fl, 0);
    return 0;
}

static int HostInit(void) {
    UINT32 Cap;

    gHostSpec = (UINT16)(Rd16(SDHCI_HOST_VERSION) & 0xFFu);
    Cap = Rd32(SDHCI_CAPABILITIES);
    Mark("Boot: SDHCI0 ");
    MarkHex("ver=", gHostSpec);
    MarkHex(" cap=", Cap);
    Mark("\n");
    return CardInit();
}

static int SdProbe(UINT32 Drive) {
    return (Drive == 0 && gReady) ? 1 : 0;
}

static int SdRead(UINT32 Drive, UINT32 Lba, UINT32 Count, void *Buffer) {
    UINT8 *P = (UINT8 *)Buffer;
    UINT32 i;

    (void)Drive;
    if (!gReady || Buffer == 0 || Count == 0) {
        return 0;
    }
    for (i = 0; i < Count; i++) {
        if (ReadOne(Lba + i, P + (UINTN)i * 512u) != 0) {
            (void)SoftReset(SDHCI_RESET_CMD | SDHCI_RESET_DATA);
            return 0;
        }
    }
    return 1;
}

static int SdWrite(UINT32 Drive, UINT32 Lba, UINT32 Count, const void *Buffer) {
    (void)Drive;
    (void)Lba;
    (void)Count;
    (void)Buffer;
    return 0;
}

static const BLOCK_BACKEND gSdBackend = {
    .Probe = SdProbe,
    .ReadSectors = SdRead,
    .WriteSectors = SdWrite,
    .Flush = 0,
};

static int SdDriverProbe(const DRIVER *Self, void *BusCtx, void **OutPrivate) {
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
    Mark(gSdhc ? "Boot: SD SDHC ready\n" : "Boot: SD SDSC ready\n");
    if (OutPrivate) {
        *OutPrivate = 0;
    }
    return 0;
}

static int SdDriverBind(DRIVER_INSTANCE *Inst) {
    (void)Inst;
    return DriverBlockAttach(&gSdBackend);
}

static void SdDriverRemove(DRIVER_INSTANCE *Inst) {
    (void)Inst;
    gReady = 0;
}

static const DRIVER gSdDriver = {
    .Name = "cv1800-sdhci",
    .Class = DRIVER_CLASS_BLOCK,
    .Match = 0,
    .Probe = SdDriverProbe,
    .Bind = SdDriverBind,
    .Remove = SdDriverRemove,
};

void BoardSdRegister(void) {
    (void)DriverRegister(&gSdDriver);
}
