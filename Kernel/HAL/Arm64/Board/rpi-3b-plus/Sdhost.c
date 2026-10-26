/*
 * Sdhost.c — Pi3B+ µSD：BCM2835 SDHOST PIO 只读 → BLOCK_BACKEND
 *
 * 主参考：U-Boot 2021.01 drivers/mmc/bcm2835_sdhost.c
 * 基址 0x3F202000（VC sdhost@7e202000 / GPIO48）。
 * Arasan SDHCI@0x3F300000 是 WiFi SDIO，本文件不碰。
 */
#include "Sdhost.h"
#include "Board.h"
#include "Block.h"
#include "Driver.h"
#include "DriverBlock.h"
#include "Hal.h"
#include "HalSerial.h"
#include "SerialLog.h"

#define ID_HZ   400000u
#define XFER_HZ 25000000u

#define RSP_NONE 0
#define RSP_R1   1
#define RSP_R1B  2
#define RSP_R2   3
#define RSP_R3   4 /* OCR：允许 CRC7 FAIL（对齐 U-Boot SEND_OP_COND） */

static UINT32 gMaxClk;
static UINT32 gHcfg;
static UINT32 gCdiv;
static UINT32 gRca;
static UINT32 gResp[4];
static int gSdhc;
static int gReady;
static UINT32 gLastCmd;
static UINT32 gLastHsts;

static UINT64 gCntFrq;

static volatile UINT32 *Reg32(UINT32 Off) {
    return (volatile UINT32 *)(UINTN)(SDHOST_BASE + Off);
}

static void Wr32(UINT32 Off, UINT32 Val) {
    *Reg32(Off) = Val;
}

static UINT32 Rd32(UINT32 Off) {
    return *Reg32(Off);
}

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

static void DelayMs(UINT32 Ms) {
    DelayUs(Ms * 1000u);
}

static void MarkHex32(const char *Tag, UINT32 V) {
    char Buf[9];
    HalSerialBootMark(Tag);
    HalSerialFormatHex(Buf, V, 8);
    HalSerialBootMark(Buf);
}

static void DumpRegs(const char *Why) {
    HalSerialBootMark(Why);
    MarkHex32(" SDCMD=", Rd32(SDCMD));
    MarkHex32(" SDHSTS=", Rd32(SDHSTS));
    MarkHex32(" SDCDIV=", Rd32(SDCDIV));
    MarkHex32(" SDEDM=", Rd32(SDEDM));
    HalSerialBootMark("\n");
}

/* —— 复位 / 时钟（对齐 bcm2835_reset_internal / set_clock）—— */

static void ResetInternal(void) {
    UINT32 Temp;

    Wr32(SDVDD, SDVDD_POWER_OFF);
    Wr32(SDCMD, 0);
    Wr32(SDARG, 0);
    Wr32(SDTOUT, 0xf00000u);
    Wr32(SDCDIV, 0);
    Wr32(SDHSTS, SDHSTS_CLEAR_MASK);
    Wr32(SDHCFG, 0);
    Wr32(SDHBCT, 0);
    Wr32(SDHBLC, 0);

    Temp = Rd32(SDEDM);
    Temp &= ~((SDEDM_THRESHOLD_MASK << SDEDM_READ_THRESHOLD_SHIFT) |
              (SDEDM_THRESHOLD_MASK << SDEDM_WRITE_THRESHOLD_SHIFT));
    Temp |= (FIFO_READ_THRESHOLD << SDEDM_READ_THRESHOLD_SHIFT) |
            (FIFO_WRITE_THRESHOLD << SDEDM_WRITE_THRESHOLD_SHIFT);
    Wr32(SDEDM, Temp);
    DelayMs(20);
    Wr32(SDVDD, SDVDD_POWER_ON);
    DelayMs(20);
    Wr32(SDHCFG, gHcfg);
    Wr32(SDCDIV, gCdiv);
}

static void SetClock(UINT32 Clock) {
    UINT32 Div;
    UINT32 Actual;

    if (Clock < 100000u) {
        gCdiv = SDCDIV_MAX_CDIV;
        Wr32(SDCDIV, gCdiv);
        return;
    }
    if (gMaxClk == 0) {
        gMaxClk = 250000000u;
    }
    Div = gMaxClk / Clock;
    if (Div < 2u) {
        Div = 2u;
    }
    if ((gMaxClk / Div) > Clock) {
        Div++;
    }
    Div -= 2u;
    if (Div > SDCDIV_MAX_CDIV) {
        Div = SDCDIV_MAX_CDIV;
    }
    Actual = gMaxClk / (Div + 2u);
    gCdiv = Div;
    Wr32(SDCDIV, gCdiv);
    Wr32(SDTOUT, Actual / 2u);
}

static void SetIos(UINT32 ClockHz, int Wide4) {
    SetClock(ClockHz);
    gHcfg &= ~SDHCFG_WIDE_EXT_BUS;
    if (Wide4) {
        gHcfg |= SDHCFG_WIDE_EXT_BUS;
    }
    gHcfg |= SDHCFG_WIDE_INT_BUS;
    gHcfg |= SDHCFG_SLOW_CARD;
    Wr32(SDHCFG, gHcfg);
}

/* —— 命令 / PIO（对齐 send_command / finish / transfer）—— */

static UINT32 WaitSdcmdClear(UINT32 TimeoutUs) {
    UINT32 Value = 0;
    UINT32 n;
    UINT32 Steps = TimeoutUs / 10u + 1u;

    for (n = 0; n < Steps; n++) {
        Value = Rd32(SDCMD);
        if ((Value & SDCMD_NEW_FLAG) == 0) {
            return Value;
        }
        DelayUs(10);
    }
    return Value;
}

static int WaitTransferComplete(void) {
    UINT32 n;

    for (n = 0; n < 10000u; n++) {
        UINT32 Edm = Rd32(SDEDM);
        UINT32 Fsm = Edm & SDEDM_FSM_MASK;

        if (Fsm == SDEDM_FSM_IDENTMODE || Fsm == SDEDM_FSM_DATAMODE) {
            return 0;
        }
        if (Fsm == SDEDM_FSM_READWAIT || Fsm == SDEDM_FSM_WRITESTART1 ||
            Fsm == SDEDM_FSM_READDATA) {
            Wr32(SDEDM, Edm | SDEDM_FORCE_DATA_MODE);
            return 0;
        }
        DelayUs(100);
    }
    DumpRegs("Boot: SDHOST xfer wait timeout");
    return -1;
}

static int TransferBlockPio(UINT8 *Buf) {
    UINT32 *Words = (UINT32 *)(void *)Buf;
    int CopyWords = 512 / 4;
    UINT32 Hsts = 0;
    UINT32 Spins = 0;

    while (CopyWords > 0) {
        int Burst = CopyWords < (int)SDDATA_FIFO_PIO_BURST ? CopyWords
                                                          : (int)SDDATA_FIFO_PIO_BURST;
        UINT32 Edm = Rd32(SDEDM);
        int Aval = (int)((Edm >> SDEDM_FIFO_FILL_SHIFT) & SDEDM_FIFO_FILL_MASK);
        int WordsNow;

        if (Aval < Burst) {
            int Fsm = (int)(Edm & SDEDM_FSM_MASK);
            if (Fsm != (int)SDEDM_FSM_READDATA && Fsm != (int)SDEDM_FSM_READWAIT &&
                Fsm != (int)SDEDM_FSM_READCRC) {
                Hsts = Rd32(SDHSTS);
                if ((Hsts & SDHSTS_ERROR_MASK) != 0) {
                    break;
                }
            }
            /* 对齐 wait_transfer_complete：约 1s 仍无字则 dump，勿空转挂死 */
            if (++Spins > 100000u) {
                DumpRegs("Boot: SDHOST PIO stall");
                return -1;
            }
            DelayUs(10);
            continue;
        }
        Spins = 0;
        WordsNow = Aval > CopyWords ? CopyWords : Aval;
        CopyWords -= WordsNow;
        while (WordsNow--) {
            *Words++ = Rd32(SDDATA);
        }
    }

    Hsts = Rd32(SDHSTS);
    if ((Hsts & (SDHSTS_CRC16_ERROR | SDHSTS_CRC7_ERROR | SDHSTS_FIFO_ERROR)) != 0) {
        gLastHsts = Hsts;
        return -1;
    }
    if ((Hsts & (SDHSTS_CMD_TIME_OUT | SDHSTS_REW_TIME_OUT)) != 0) {
        gLastHsts = Hsts;
        return -1;
    }
    return 0;
}

/*
 * Cmd + 可选单块读。RspType=RSP_*；DataBuf!=0 → CMD17 形读 512B。
 */
static int SendCmd(UINT8 CmdIdx, UINT32 Arg, int RspType, UINT8 *DataBuf) {
    UINT32 Sdcmd;
    UINT32 Sdhsts;
    UINT32 Edm;
    UINT32 Fsm;
    int UseBusy = 0;
    int Ret;

    Edm = Rd32(SDEDM);
    Fsm = Edm & SDEDM_FSM_MASK;
    if (Fsm != SDEDM_FSM_IDENTMODE && Fsm != SDEDM_FSM_DATAMODE && CmdIdx != 12) {
        DumpRegs("Boot: SDHOST FSM busy");
        return -1;
    }

    Sdcmd = WaitSdcmdClear(100000u);
    if ((Sdcmd & SDCMD_NEW_FLAG) != 0) {
        DumpRegs("Boot: SDHOST SDCMD stuck");
        return -1;
    }

    Sdhsts = Rd32(SDHSTS);
    if ((Sdhsts & SDHSTS_ERROR_MASK) != 0) {
        Wr32(SDHSTS, Sdhsts);
    }

    if (DataBuf != 0) {
        Wr32(SDHBCT, 512u);
        Wr32(SDHBLC, 1u);
    }

    Wr32(SDARG, Arg);

    Sdcmd = (UINT32)CmdIdx & SDCMD_CMD_MASK;
    if (RspType == RSP_NONE) {
        Sdcmd |= SDCMD_NO_RESPONSE;
    } else {
        if (RspType == RSP_R2) {
            Sdcmd |= SDCMD_LONG_RESPONSE;
        }
        if (RspType == RSP_R1B) {
            Sdcmd |= SDCMD_BUSYWAIT;
            UseBusy = 1;
        }
    }
    if (DataBuf != 0) {
        Sdcmd |= SDCMD_READ_CMD;
    }

    Wr32(SDCMD, Sdcmd | SDCMD_NEW_FLAG);

    if (!UseBusy) {
        Sdcmd = WaitSdcmdClear(100000u);
        if ((Sdcmd & SDCMD_NEW_FLAG) != 0) {
            gLastCmd = Sdcmd;
            gLastHsts = Rd32(SDHSTS);
            return -1;
        }
        if ((Sdcmd & SDCMD_FAIL_FLAG) != 0) {
            Sdhsts = Rd32(SDHSTS);
            Wr32(SDHSTS, SDHSTS_ERROR_MASK);
            gLastCmd = Sdcmd;
            gLastHsts = Sdhsts;
            /* ACMD41/CMD1 OCR：CRC7 可忽略 */
            if (!((Sdhsts & SDHSTS_CRC7_ERROR) != 0 &&
                  (CmdIdx == 41 || CmdIdx == 1) &&
                  (Sdhsts & ~SDHSTS_CRC7_ERROR & SDHSTS_ERROR_MASK) == 0)) {
                return -1;
            }
        }
        if (RspType == RSP_R2) {
            gResp[3] = Rd32(SDRSP0);
            gResp[2] = Rd32(SDRSP1);
            gResp[1] = Rd32(SDRSP2);
            gResp[0] = Rd32(SDRSP3);
        } else if (RspType != RSP_NONE) {
            gResp[0] = Rd32(SDRSP0);
        }
    } else {
        /* R1b：等 BUSY_IRPT（SDHCFG_BUSY_IRPT_EN） */
        UINT32 n;
        for (n = 0; n < 10000u; n++) {
            Sdhsts = Rd32(SDHSTS);
            if ((Sdhsts & SDHSTS_ERROR_MASK) != 0) {
                Wr32(SDHSTS, SDHSTS_ERROR_MASK);
                gLastHsts = Sdhsts;
                return -1;
            }
            if ((Sdhsts & SDHSTS_BUSY_IRPT) != 0) {
                Wr32(SDHSTS, SDHSTS_BUSY_IRPT);
                break;
            }
            DelayUs(100);
        }
        if (n >= 10000u) {
            gLastHsts = Rd32(SDHSTS);
            return -1;
        }
        Sdcmd = WaitSdcmdClear(100000u);
        if ((Sdcmd & (SDCMD_NEW_FLAG | SDCMD_FAIL_FLAG)) != 0) {
            gLastCmd = Sdcmd;
            gLastHsts = Rd32(SDHSTS);
            return -1;
        }
        gResp[0] = Rd32(SDRSP0);
    }

    if (DataBuf != 0) {
        Ret = TransferBlockPio(DataBuf);
        if (Ret != 0) {
            return Ret;
        }
        return WaitTransferComplete();
    }
    return 0;
}

static int ReadOne(UINT32 Lba, UINT8 *Buf) {
    UINT32 Arg = gSdhc ? Lba : (Lba * 512u);
    return SendCmd(17, Arg, RSP_R1, Buf);
}

static int CardInit(void) {
    UINT32 Ocr;
    UINT32 i;
    int Cmd8Ok;
    UINT32 AcmdArg;

    HalSerialBootMark("Boot: SDHOST reset\n");
    gHcfg = SDHCFG_BUSY_IRPT_EN;
    gCdiv = 0;
    ResetInternal();
    SetIos(ID_HZ, 0);
    DelayUs(2000);

    (void)SendCmd(0, 0, RSP_NONE, 0);
    DelayUs(10000);

    HalSerialBootMark("Boot: SDHOST CMD8\n");
    Cmd8Ok = 0;
    if (SendCmd(8, 0x1AAu, RSP_R1, 0) == 0 && (gResp[0] & 0xFFFu) == 0x1AAu) {
        Cmd8Ok = 1;
    } else {
        MarkHex32("Boot: SDHOST CMD8 fail hsts=", gLastHsts);
        HalSerialBootMark(" → SDv1\n");
        (void)SendCmd(0, 0, RSP_NONE, 0);
        DelayUs(5000);
    }

    gSdhc = 0;
    AcmdArg = Cmd8Ok ? 0x40300000u : 0x00300000u;
    Ocr = 0;
    for (i = 0; i < 1000u; i++) {
        if (SendCmd(55, 0, RSP_R1, 0) != 0) {
            MarkHex32("Boot: SDHOST CMD55 fail hsts=", gLastHsts);
            HalSerialBootMark("\n");
            return -1;
        }
        if (SendCmd(41, AcmdArg, RSP_R3, 0) != 0) {
            MarkHex32("Boot: SDHOST ACMD41 fail hsts=", gLastHsts);
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
        HalSerialBootMark("Boot: SDHOST ACMD41 timeout\n");
        return -1;
    }

    if (SendCmd(2, 0, RSP_R2, 0) != 0) {
        HalSerialBootMark("Boot: SDHOST CMD2 fail\n");
        return -1;
    }
    if (SendCmd(3, 0, RSP_R1, 0) != 0) {
        HalSerialBootMark("Boot: SDHOST CMD3 fail\n");
        return -1;
    }
    gRca = gResp[0] & 0xFFFF0000u;

    if (SendCmd(9, gRca, RSP_R2, 0) != 0) {
        HalSerialBootMark("Boot: SDHOST CMD9 fail\n");
        return -1;
    }
    if (SendCmd(7, gRca, RSP_R1B, 0) != 0) {
        HalSerialBootMark("Boot: SDHOST CMD7 fail\n");
        return -1;
    }

    /* 1-bit 先过读（计划：4-bit 可选） */
    SetIos(XFER_HZ, 0);

    if (SendCmd(16, 512u, RSP_R1, 0) != 0) {
        /* SDHC 可忽略 */
    }
    return 0;
}

static int HostInit(void) {
    UINT32 Hz;

    /* U-Boot get_mmc_clock 亦先 power SDHCI 域；跟同源 */
    (void)BoardSdhciPowerOn();
    DelayUs(10000);

    if (BoardMboxGetClockHz(BOARD_MBOX_CLOCK_CORE, &Hz) == 0 && Hz != 0) {
        gMaxClk = Hz;
    } else {
        gMaxClk = 250000000u;
        HalSerialBootMark("Boot: SDHOST clk fallback 250M\n");
    }
    HalSerialBootMark("Boot: SDHOST @3F202000 core=");
    {
        char Buf[9];
        HalSerialFormatHex(Buf, gMaxClk, 8);
        HalSerialBootMark(Buf);
    }
    HalSerialBootMark("Hz\n");

    return CardInit();
}

static int SdhostProbe(UINT32 Drive) {
    return (Drive == 0 && gReady) ? 1 : 0;
}

static int SdhostRead(UINT32 Drive, UINT32 Lba, UINT32 Count, void *Buffer) {
    UINT8 *P = (UINT8 *)Buffer;
    UINT32 i;

    (void)Drive;
    if (!gReady || !Buffer || Count == 0) {
        return 0;
    }
    for (i = 0; i < Count; i++) {
        if (ReadOne(Lba + i, P + (UINTN)i * 512u) != 0) {
            (void)WaitTransferComplete();
            MarkHex32("Boot: SDHOST read fail hsts=", gLastHsts);
            HalSerialBootMark("\n");
            return 0;
        }
    }
    return 1;
}

static int SdhostWrite(UINT32 Drive, UINT32 Lba, UINT32 Count, const void *Buffer) {
    (void)Drive;
    (void)Lba;
    (void)Count;
    (void)Buffer;
    return 0;
}

static const BLOCK_BACKEND gSdhostBackend = {
    .Probe = SdhostProbe,
    .ReadSectors = SdhostRead,
    .WriteSectors = SdhostWrite,
    .Flush = 0,
};

static int SdhostDriverProbe(const DRIVER *Self, void *BusCtx, void **OutPrivate) {
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
    HalSerialBootMark(gSdhc ? "Boot: SDHOST SDHC ready\n" : "Boot: SDHOST SDSC ready\n");
    if (OutPrivate) {
        *OutPrivate = 0;
    }
    return 0;
}

static int SdhostDriverBind(DRIVER_INSTANCE *Inst) {
    (void)Inst;
    return DriverBlockAttach(&gSdhostBackend);
}

static void SdhostDriverRemove(DRIVER_INSTANCE *Inst) {
    (void)Inst;
    gReady = 0;
}

static const DRIVER gSdhostDriver = {
    .Name = "bcm-sdhost",
    .Class = DRIVER_CLASS_BLOCK,
    .Match = 0,
    .Probe = SdhostDriverProbe,
    .Bind = SdhostDriverBind,
    .Remove = SdhostDriverRemove,
};

void BoardSdhostRegister(void) {
    (void)DriverRegister(&gSdhostDriver);
}
