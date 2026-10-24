/*
 * Dwc2.c — Pi3B+ DWC2 host（rpi-7）
 *
 * rpi-7：控制传输 + hub 枚举 + SPLIT HID boot 鼠/键 → DriverInputAttach。
 * 传输形：U-Boot dwc2；HCSPLT.XACTPOS=ALL（Synopsys 非周期 SPLIT）；
 * hub 类请求对齐 HAL/X64 Hub/Request.c；键解析对齐 X64 HidParse（拒假键）。
 */
#include "Dwc2.h"
#include "Board.h"
#include "Hal.h"
#include "SerialLog.h"
#include "DriverInput.h"

#define DWC2_DMA_BUF_SIZE  512u
#define DWC2_CTRL_RETRY    8u
/* BCM2837：DWC2/VC 见总线别名（U-Boot phys_to_bus；同 Mbox PHYS_TO_BUS） */
#define DWC2_PHYS_TO_BUS(P) (((UINT32)(UINTN)(P) & 0x3FFFFFFFu) | 0xC0000000u)

typedef struct {
    UINT8 BmRequestType;
    UINT8 BRequest;
    UINT16 WValue;
    UINT16 WIndex;
    UINT16 WLength;
} __attribute__((packed)) UsbSetup;

static volatile UINT32 *Dwc2Reg(UINT32 Off) {
    return (volatile UINT32 *)(UINTN)(DWC2_BASE + Off);
}

static void Dwc2Spin(UINT32 N) {
    volatile UINT32 i;
    for (i = 0; i < N; i++) {
        __asm__ volatile("nop");
    }
}

static int Dwc2WaitBits(UINT32 Off, UINT32 Mask, UINT32 Want, UINT32 Spins) {
    UINT32 i;
    for (i = 0; i < Spins; i++) {
        if ((*Dwc2Reg(Off) & Mask) == Want) {
            return 0;
        }
        Dwc2Spin(100);
    }
    return -1;
}

static void Hex32(UINT32 V) {
    char Buf[9];
    HalSerialFormatHex(Buf, V, 8);
    HalSerialBootMark(Buf);
}

static void Hex16(UINT16 V) {
    char Buf[5];
    HalSerialFormatHex(Buf, V, 4);
    HalSerialBootMark(Buf);
}

static void Dwc2DelayMs(UINT32 Ms) {
    UINT32 m;
    for (m = 0; m < Ms; m++) {
        Dwc2Spin(80000u);
    }
}

static void HprtWrite(UINT32 Val) {
    Val &= ~DWC2_HPRT0_W1C_MASK;
    *Dwc2Reg(DWC2_HPRT0) = Val;
}

/* skip-MMU：VA==PA；仍 civac 供 DWC2 DMA 见缓冲 */
static UINT8 gDmaBuf[DWC2_DMA_BUF_SIZE] __attribute__((aligned(64)));
static UINT8 gSetupBuf[8] __attribute__((aligned(64)));
static UINT8 gStatusBuf[64] __attribute__((aligned(64)));
static UINT32 gRootSpd; /* HPRT PRTSPD_* */
static UINT16 gEp0Mps = 64;
static UINT8 gHubAddr;
/* 当前控制/中断目标：FS/LS 在 HS 根口后须 SPLIT */
static UINT8 gXferHub;
static UINT8 gXferPort;
static int gXferDoSplit;
static int gXferLowSpeed;
static UINT16 gXferEp0Mps = 64;

/* HID boot 鼠/键（Poll 先鼠后键） */
static int gMouseOk;
static UINT8 gMouseAddr;
static UINT8 gMouseEp;
static UINT16 gMouseMps;
static UINT8 gMouseToggle;
static UINT8 gMouseHub;
static UINT8 gMousePort;
static int gMouseSplit;
static int gMouseLow;
static UINT32 gMouseX;
static UINT32 gMouseY;
static int gMouseAbsInit;
#define MOUSE_Q 16
static UINT32 gMqX[MOUSE_Q];
static UINT32 gMqY[MOUSE_Q];
static UINT8 gMqB[MOUSE_Q];
static INT8 gMqW[MOUSE_Q];
static UINT8 gMqH;
static UINT8 gMqT;

static int gKbdOk;
static UINT8 gKbdAddr;
static UINT8 gKbdEp;
static UINT16 gKbdMps;
static UINT8 gKbdToggle;
static UINT8 gKbdHub;
static UINT8 gKbdPort;
static int gKbdSplit;
static int gKbdLow;
#define KBD_Q 16
static HAL_KEYBOARD_REPORT gKq[KBD_Q];
static UINT8 gKqH;
static UINT8 gKqT;

#define HUB_FEAT_PORT_RESET        4u
#define HUB_FEAT_PORT_POWER        8u
#define HUB_FEAT_C_PORT_CONNECTION 16u
#define HUB_FEAT_C_PORT_RESET      20u
#define HUB_FEAT_C_PORT_ENABLE     17u
#define HUB_STAT_CONNECT           (1u << 0)
#define HUB_STAT_ENABLE            (1u << 1)
#define HUB_STAT_RESET             (1u << 4)
#define HUB_STAT_LOW_SPEED         (1u << 9)
#define HUB_STAT_HIGH_SPEED        (1u << 10)

static void FlushTxFifo(UINT32 Num) {
    *Dwc2Reg(DWC2_GRSTCTL) =
        DWC2_GRSTCTL_TXFFLSH | (Num << DWC2_GRSTCTL_TXFNUM_SHIFT);
    (void)Dwc2WaitBits(DWC2_GRSTCTL, DWC2_GRSTCTL_TXFFLSH, 0, 100000u);
    Dwc2Spin(1000);
}

static void FlushRxFifo(void) {
    *Dwc2Reg(DWC2_GRSTCTL) = DWC2_GRSTCTL_RXFFLSH;
    (void)Dwc2WaitBits(DWC2_GRSTCTL, DWC2_GRSTCTL_RXFFLSH, 0, 100000u);
    Dwc2Spin(1000);
}

static void HaltChannels(void) {
    UINT32 N;
    UINT32 i;
    UINT32 Char;

    N = (*Dwc2Reg(DWC2_GHWCFG2) & DWC2_GHWCFG2_NUM_HOST_CHAN_MASK) >>
        DWC2_GHWCFG2_NUM_HOST_CHAN_SHIFT;
    N += 1u;
    if (N > 16u) {
        N = 16u;
    }
    for (i = 0; i < N; i++) {
        Char = *Dwc2Reg(DWC2_HCCHAR(i));
        Char &= ~(DWC2_HCCHAR_CHEN | DWC2_HCCHAR_EPDIR);
        Char |= DWC2_HCCHAR_CHDIS;
        *Dwc2Reg(DWC2_HCCHAR(i)) = Char;
    }
    for (i = 0; i < N; i++) {
        Char = *Dwc2Reg(DWC2_HCCHAR(i));
        Char &= ~DWC2_HCCHAR_EPDIR;
        Char |= DWC2_HCCHAR_CHEN | DWC2_HCCHAR_CHDIS;
        *Dwc2Reg(DWC2_HCCHAR(i)) = Char;
        (void)Dwc2WaitBits(DWC2_HCCHAR(i), DWC2_HCCHAR_CHEN, 0, 50000u);
    }
}

static int RootPortReset(void) {
    UINT32 Hprt;
    UINT32 i;

    Hprt = *Dwc2Reg(DWC2_HPRT0);
    Hprt |= DWC2_HPRT0_PRTRST;
    HprtWrite(Hprt);
    Dwc2DelayMs(60);
    Hprt = *Dwc2Reg(DWC2_HPRT0);
    Hprt &= ~DWC2_HPRT0_PRTRST;
    HprtWrite(Hprt);

    for (i = 0; i < 100u; i++) {
        Hprt = *Dwc2Reg(DWC2_HPRT0);
        if ((Hprt & DWC2_HPRT0_PRTENA) != 0) {
            return 0;
        }
        Dwc2DelayMs(10);
    }
    return -1;
}

static void HcInit(UINT8 DevAddr, UINT8 EpNum, int EpIn, UINT8 EpType,
                   UINT16 Mps, int LowSpeed, int DoSplit) {
    UINT32 Hcchar;

    Hcchar = ((UINT32)DevAddr << DWC2_HCCHAR_DEVADDR_SHIFT) |
             ((UINT32)EpNum << DWC2_HCCHAR_EPNUM_SHIFT) |
             ((UINT32)EpType << DWC2_HCCHAR_EPTYPE_SHIFT) |
             ((UINT32)Mps << DWC2_HCCHAR_MPS_SHIFT);
    if (EpIn) {
        Hcchar |= DWC2_HCCHAR_EPDIR;
    }
    if (LowSpeed) {
        Hcchar |= DWC2_HCCHAR_LSPDDEV;
    }
    *Dwc2Reg(DWC2_HCCHAR(DWC2_HC_CHANNEL)) = Hcchar;
    if (DoSplit) {
        /* Synopsys：控制/批量 SPLIT 的 XACTPOS=ALL；0(MID) 仅周期中间包 */
        *Dwc2Reg(DWC2_HCSPLT(DWC2_HC_CHANNEL)) =
            DWC2_HCSPLT_SPLTENA | DWC2_HCSPLT_XACTPOS_ALL |
            ((UINT32)gXferHub << DWC2_HCSPLT_HUBADDR_SHIFT) |
            ((UINT32)gXferPort << DWC2_HCSPLT_PRTADDR_SHIFT);
    } else {
        *Dwc2Reg(DWC2_HCSPLT(DWC2_HC_CHANNEL)) = 0;
    }
}

/*
 * 同 U-Boot wait_for_chhltd：仅 XFERCOMP 算成功。
 * SSPLIT 的 ACK 在 ChunkMsg 里收（真机屏曾见 HCINT=0x82=CHHLTD|XACTERR）。
 */
static int WaitChhltd(UINT32 *SubOut, UINT8 *PidOut) {
    UINT32 i;
    UINT32 Hcint;
    UINT32 Hctsiz;
    UINT32 Off = DWC2_HCINT(DWC2_HC_CHANNEL);

    for (i = 0; i < 200000u; i++) {
        if ((*Dwc2Reg(Off) & DWC2_HCINT_CHHLTD) != 0) {
            break;
        }
        Dwc2Spin(50);
    }
    if ((*Dwc2Reg(Off) & DWC2_HCINT_CHHLTD) == 0) {
        return -1;
    }

    Hcint = *Dwc2Reg(Off);
    Hctsiz = *Dwc2Reg(DWC2_HCTSIZ(DWC2_HC_CHANNEL));
    *SubOut = Hctsiz & 0x7FFFFu;
    *PidOut = (UINT8)((Hctsiz >> DWC2_HCTSIZ_PID_SHIFT) & 3u);

    if ((Hcint & DWC2_HCINT_XFERCOMP) != 0) {
        return 0;
    }
    if ((Hcint & DWC2_HCINT_NYET) != 0) {
        return -3; /* CSPLIT NYET */
    }
    if ((Hcint & (DWC2_HCINT_NAK | DWC2_HCINT_FRMOVRUN)) != 0) {
        return -2;
    }
    /* ACK / XACTERR / STALL 等：交 ChunkMsg 看 HCINT（U-Boot 同形） */
    return -1;
}

static int TransferChunk(UINT8 *Pid, int In, void *Buffer, UINT32 NumPkts,
                         UINT32 XferLen, UINT32 *ActualLen, int OddFrame) {
    UINT32 Sub;
    int Ret;

    *Dwc2Reg(DWC2_HCTSIZ(DWC2_HC_CHANNEL)) =
        (XferLen << DWC2_HCTSIZ_XFERSIZE_SHIFT) |
        (NumPkts << DWC2_HCTSIZ_PKTCNT_SHIFT) |
        ((UINT32)(*Pid) << DWC2_HCTSIZ_PID_SHIFT);

    if (XferLen != 0) {
        if (!In) {
            UINT32 i;
            for (i = 0; i < XferLen && i < DWC2_DMA_BUF_SIZE; i++) {
                gDmaBuf[i] = ((UINT8 *)Buffer)[i];
            }
            HalDcacheCleanInvalidateRange(gDmaBuf, XferLen);
        } else {
            HalDcacheCleanInvalidateRange(gDmaBuf, XferLen);
        }
    }

    *Dwc2Reg(DWC2_HCDMA(DWC2_HC_CHANNEL)) = DWC2_PHYS_TO_BUS(gDmaBuf);
    *Dwc2Reg(DWC2_HCINT(DWC2_HC_CHANNEL)) = 0x3FFFu;

    {
        UINT32 Hcchar = *Dwc2Reg(DWC2_HCCHAR(DWC2_HC_CHANNEL));
        Hcchar &= ~(DWC2_HCCHAR_CHEN | DWC2_HCCHAR_CHDIS | DWC2_HCCHAR_ODDFRM |
                    (3u << DWC2_HCCHAR_MULTICNT_SHIFT));
        Hcchar |= (1u << DWC2_HCCHAR_MULTICNT_SHIFT) | DWC2_HCCHAR_CHEN;
        if (OddFrame) {
            Hcchar |= DWC2_HCCHAR_ODDFRM;
        }
        *Dwc2Reg(DWC2_HCCHAR(DWC2_HC_CHANNEL)) = Hcchar;
    }

    Ret = WaitChhltd(&Sub, Pid);
    if (Ret < 0) {
        return Ret;
    }

    if (In && XferLen != 0) {
        UINT32 Got = XferLen - Sub;
        UINT32 i;
        if (Got > XferLen) {
            Got = XferLen;
        }
        HalDcacheCleanInvalidateRange(gDmaBuf, Got ? Got : 1u);
        for (i = 0; i < Got; i++) {
            ((UINT8 *)Buffer)[i] = gDmaBuf[i];
        }
        *ActualLen = Got;
    } else {
        *ActualLen = XferLen;
    }
    return 0;
}

/*
 * U-Boot chunk_msg + EDK2 DwUsbHost：每包 HcInit；SSPLIT ACK→CSPLIT；
 * CSPLIT NYET 同帧内重试；XACTERR 整段重试（屏上曾见 HCINT=0x82）。
 */
static int ChunkMsg(UINT8 DevAddr, UINT8 EpNum, UINT8 EpType, int In,
                    UINT8 *Pid, void *Buffer, int Len, UINT16 Mps) {
    int Done = 0;
    int CompleteSplit = 0;
    UINT32 SsplitFrame = 0;
    int XactTries = 0;
    int NyetTries = 0;

    if (Mps == 0) {
        Mps = gXferEp0Mps;
    }

    for (;;) {
        UINT32 Xfer;
        UINT32 Npkts;
        UINT32 Actual = 0;
        int Ret;
        int Odd = 0;
        UINT32 Hcint;
        UINT32 Spl;

        if (Len == 0) {
            Xfer = 0;
            Npkts = 1;
        } else {
            Xfer = (UINT32)(Len - Done);
            if (Xfer > Mps) {
                Xfer = Mps;
            }
            Npkts = 1;
        }

        /* EDK2：每包重装 HCCHAR/HCSPLT（含 CSPLIT 切换） */
        HcInit(DevAddr, EpNum, In, EpType, Mps, gXferLowSpeed, gXferDoSplit);
        if (gXferDoSplit) {
            Spl = *Dwc2Reg(DWC2_HCSPLT(DWC2_HC_CHANNEL));
            if (CompleteSplit) {
                Spl |= DWC2_HCSPLT_COMPSPLT;
                /*
                 * USB2 §11.18：CSPLIT 须在 SSPLIT 之后的微帧。
                 * LS 经 TT 更敏感；同微帧立刻 CSPLIT → 真机 DATATGLERR(HCINT bit10)。
                 */
                {
                    UINT32 Guard = 0;
                    while (((*Dwc2Reg(DWC2_HFNUM) & 0x3FFFu) == SsplitFrame) &&
                           Guard < 20000u) {
                        Dwc2Spin(50);
                        Guard++;
                    }
                }
            } else {
                Spl &= ~DWC2_HCSPLT_COMPSPLT;
            }
            *Dwc2Reg(DWC2_HCSPLT(DWC2_HC_CHANNEL)) = Spl;
        }

        if (EpType == DWC2_HCCHAR_EPTYPE_INTR) {
            Odd = ((*Dwc2Reg(DWC2_HFNUM) & 1u) == 0) ? 1 : 0;
        }

        Dwc2Spin(gXferLowSpeed ? 16000u : 8000u); /* LS 多 settle */
        Ret = TransferChunk(Pid, In,
                            Len ? (void *)((UINT8 *)Buffer + Done) : gStatusBuf,
                            Npkts, Xfer, &Actual, Odd);
        Hcint = *Dwc2Reg(DWC2_HCINT(DWC2_HC_CHANNEL));

        if (CompleteSplit) {
            if (Ret == -3 || (Hcint & DWC2_HCINT_NYET) != 0) {
                UINT32 Fn = *Dwc2Reg(DWC2_HFNUM) & 0x3FFFu;
                if (((Fn - SsplitFrame) & 0x3FFFu) > 4u) {
                    return -2;
                }
                if (++NyetTries > 3) {
                    CompleteSplit = 0;
                    NyetTries = 0;
                    continue; /* EDK2 RestartXfer */
                }
                Ret = 0;
                Actual = 0;
            } else if (Ret != 0) {
                /* fall through to error / xact retry */
            } else {
                CompleteSplit = 0;
                NyetTries = 0;
            }
        } else if (gXferDoSplit && (Hcint & DWC2_HCINT_ACK) != 0) {
            SsplitFrame = *Dwc2Reg(DWC2_HFNUM) & 0x3FFFu;
            CompleteSplit = 1;
            NyetTries = 0;
            Actual = 0;
            Ret = 0;
        }

        if (Ret == -2) {
            return -2;
        }
        if (Ret != 0) {
            /* XACTERR / DATATGLERR：整段从 SSPLIT 重来（LS getdesc18 真机见 0x402） */
            if (gXferDoSplit &&
                (Hcint & (DWC2_HCINT_XACTERR | DWC2_HCINT_DATATGLERR)) != 0 &&
                XactTries < 3) {
                XactTries++;
                CompleteSplit = 0;
                NyetTries = 0;
                Dwc2DelayMs(2);
                continue;
            }
            HalSerialBootMark("Boot: DWC2 HCINT=");
            Hex32(Hcint);
            HalSerialBootMark(" spl=");
            Hex32(*Dwc2Reg(DWC2_HCSPLT(DWC2_HC_CHANNEL)));
            HalSerialBootMark(" hub=");
            Hex16(gXferHub);
            HalSerialBootMark("+");
            Hex16(gXferPort);
            HalSerialBootMark("\n");
            return Ret;
        }
        XactTries = 0;
        Done += (int)Actual;
        if (CompleteSplit) {
            continue;
        }
        if (Len == 0) {
            break;
        }
        if (Actual < Xfer) {
            break;
        }
        if (Done >= Len) {
            break;
        }
    }
    *Dwc2Reg(DWC2_HCINTMSK(DWC2_HC_CHANNEL)) = 0;
    *Dwc2Reg(DWC2_HCINT(DWC2_HC_CHANNEL)) = 0xFFFFFFFFu;
    return Done;
}

static void XferTargetHub(void) {
    gXferDoSplit = 0;
    gXferLowSpeed = 0;
    gXferHub = 0;
    gXferPort = 0;
    gXferEp0Mps = gEp0Mps;
}

static void XferTargetSplit(UINT8 Hub, UINT8 Port, int Low, UINT16 Ep0Mps) {
    gXferDoSplit = (gRootSpd == DWC2_HPRT0_PRTSPD_HS) ? 1 : 0;
    gXferLowSpeed = Low;
    gXferHub = Hub;
    gXferPort = Port;
    gXferEp0Mps = Ep0Mps ? Ep0Mps : (Low ? 8u : 64u);
}

static int ControlMsg(UINT8 DevAddr, UsbSetup *Setup, void *Data, int Len) {
    UINT8 Pid;
    int Ret;
    int Act = 0;
    int StatusIn;
    int Try;
    UINT16 Mps = gXferEp0Mps;

    for (Try = 0; Try < (int)DWC2_CTRL_RETRY; Try++) {
        UINT32 i;
        Pid = DWC2_HC_PID_SETUP;
        for (i = 0; i < 8; i++) {
            gSetupBuf[i] = ((UINT8 *)Setup)[i];
        }
        Ret = ChunkMsg(DevAddr, 0, DWC2_HCCHAR_EPTYPE_CTRL, 0, &Pid, gSetupBuf,
                       8, Mps);
        if (Ret == -2) {
            continue;
        }
        if (Ret < 0) {
            return Ret;
        }
        break;
    }
    if (Try >= (int)DWC2_CTRL_RETRY) {
        return -1;
    }

    if (Data != 0 && Len > 0) {
        int In = (Setup->BmRequestType & 0x80u) != 0;
        for (Try = 0; Try < (int)DWC2_CTRL_RETRY; Try++) {
            Pid = DWC2_HC_PID_DATA1;
            Ret = ChunkMsg(DevAddr, 0, DWC2_HCCHAR_EPTYPE_CTRL, In, &Pid, Data,
                           Len, Mps);
            if (Ret == -2) {
                continue;
            }
            if (Ret < 0) {
                return Ret;
            }
            Act = Ret;
            break;
        }
        if (Try >= (int)DWC2_CTRL_RETRY) {
            return -1;
        }
        StatusIn = In ? 0 : 1;
    } else {
        StatusIn = 1;
    }

    for (Try = 0; Try < (int)DWC2_CTRL_RETRY; Try++) {
        Pid = DWC2_HC_PID_DATA1;
        Ret = ChunkMsg(DevAddr, 0, DWC2_HCCHAR_EPTYPE_CTRL, StatusIn, &Pid,
                       gStatusBuf, 0, Mps);
        if (Ret == -2) {
            continue;
        }
        if (Ret < 0) {
            return Ret;
        }
        break;
    }
    if (Try >= (int)DWC2_CTRL_RETRY) {
        return -1;
    }
    return Act;
}

static int HubGetPortStatus(UINT8 Hub, UINT8 Port, UINT16 *St, UINT16 *Ch) {
    UsbSetup Setup;
    UINT8 Buf[4];
    int Ret;

    XferTargetHub();
    Setup.BmRequestType = 0xA3;
    Setup.BRequest = 0;
    Setup.WValue = 0;
    Setup.WIndex = Port;
    Setup.WLength = 4;
    Ret = ControlMsg(Hub, &Setup, Buf, 4);
    if (Ret < 4) {
        return -1;
    }
    *St = (UINT16)(Buf[0] | ((UINT16)Buf[1] << 8));
    *Ch = (UINT16)(Buf[2] | ((UINT16)Buf[3] << 8));
    return 0;
}

static int HubSetPortFeat(UINT8 Hub, UINT8 Port, UINT16 Feat, int Set) {
    UsbSetup Setup;

    XferTargetHub();
    Setup.BmRequestType = 0x23;
    Setup.BRequest = Set ? 0x03u : 0x01u;
    Setup.WValue = Feat;
    Setup.WIndex = Port;
    Setup.WLength = 0;
    return ControlMsg(Hub, &Setup, 0, 0) < 0 ? -1 : 0;
}

static int HubResetPort(UINT8 Hub, UINT8 Port, UINT16 *StOut) {
    UINT16 St;
    UINT16 Ch;
    UINT32 i;

    if (HubSetPortFeat(Hub, Port, HUB_FEAT_PORT_RESET, 1) != 0) {
        return -1;
    }
    for (i = 0; i < 50u; i++) {
        if (HubGetPortStatus(Hub, Port, &St, &Ch) != 0) {
            return -1;
        }
        if ((St & HUB_STAT_RESET) == 0) {
            break;
        }
        Dwc2DelayMs(10);
    }
    (void)HubSetPortFeat(Hub, Port, HUB_FEAT_C_PORT_RESET, 0);
    (void)HubSetPortFeat(Hub, Port, HUB_FEAT_C_PORT_ENABLE, 0);
    Dwc2DelayMs(20);
    if (HubGetPortStatus(Hub, Port, &St, &Ch) != 0) {
        return -1;
    }
    if ((St & HUB_STAT_ENABLE) == 0) {
        return -1;
    }
    *StOut = St;
    return 0;
}

static int ParseBootMouse(UINT8 *Cfg, UINT16 Total, UINT8 *Iface, UINT8 *Ep,
                          UINT16 *Mps, UINT8 *ScoreOut) {
    UINT16 Off = 0;
    UINT8 Best = 0;
    UINT8 CurScore = 0;
    UINT8 CurIface = 0;

    *Iface = 0;
    *Ep = 0;
    *Mps = 8;
    if (ScoreOut) {
        *ScoreOut = 0;
    }
    while (Off + 2 <= Total) {
        UINT8 Len = Cfg[Off];
        UINT8 Type = Cfg[Off + 1];
        if (Len < 2 || Off + Len > Total) {
            break;
        }
        if (Type == 4 && Len >= 9) {
            UINT8 Cls = Cfg[Off + 5];
            UINT8 Sub = Cfg[Off + 6];
            UINT8 Proto = Cfg[Off + 7];
            CurScore = 0;
            /* 鼠：Proto≠1（键）；3/1/2 最优 */
            if (Cls == 3 && Proto != 1) {
                CurIface = Cfg[Off + 2];
                if (Sub == 1 && Proto == 2) {
                    CurScore = 3;
                } else if (Sub == 1) {
                    CurScore = 2;
                } else {
                    CurScore = 1;
                }
            }
        } else if (Type == 5 && Len >= 7 && CurScore != 0) {
            UINT8 Addr = Cfg[Off + 2];
            UINT8 Attr = Cfg[Off + 3];
            if ((Addr & 0x80u) != 0 && (Attr & 3u) == 3u && CurScore > Best) {
                Best = CurScore;
                *Iface = CurIface;
                *Ep = Addr;
                *Mps = (UINT16)(Cfg[Off + 4] | ((UINT16)Cfg[Off + 5] << 8));
                if (Best == 3) {
                    if (ScoreOut) {
                        *ScoreOut = Best;
                    }
                    return 1;
                }
            }
        }
        Off = (UINT16)(Off + Len);
    }
    if (ScoreOut) {
        *ScoreOut = Best;
    }
    return Best != 0;
}

/* 对齐 X64 ParseConfig：3/1/1 最优；弱分+像样鼠 → 调用方拒认（G102 假键） */
static int ParseBootKeyboard(UINT8 *Cfg, UINT16 Total, UINT8 *Iface, UINT8 *Ep,
                             UINT16 *Mps, UINT8 *ScoreOut) {
    UINT16 Off = 0;
    UINT8 Best = 0;
    UINT8 CurScore = 0;
    UINT8 CurIface = 0;

    *Iface = 0;
    *Ep = 0;
    *Mps = 8;
    if (ScoreOut) {
        *ScoreOut = 0;
    }
    while (Off + 2 <= Total) {
        UINT8 Len = Cfg[Off];
        UINT8 Type = Cfg[Off + 1];
        if (Len < 2 || Off + Len > Total) {
            break;
        }
        if (Type == 4 && Len >= 9) {
            UINT8 Cls = Cfg[Off + 5];
            UINT8 Sub = Cfg[Off + 6];
            UINT8 Proto = Cfg[Off + 7];
            CurScore = 0;
            if (Cls == 3 && Sub == 1 && Proto == 1) {
                CurScore = 3;
            } else if (Cls == 3 && Sub == 1 && Proto == 0) {
                CurScore = 2;
            } else if (Cls == 3 && Sub != 1) {
                CurScore = 1;
            }
            CurIface = Cfg[Off + 2];
        } else if (Type == 5 && Len >= 7 && CurScore != 0) {
            UINT8 Addr = Cfg[Off + 2];
            UINT8 Attr = Cfg[Off + 3];
            if ((Addr & 0x80u) != 0 && (Attr & 3u) == 3u && CurScore > Best) {
                Best = CurScore;
                *Iface = CurIface;
                *Ep = Addr;
                *Mps = (UINT16)(Cfg[Off + 4] | ((UINT16)Cfg[Off + 5] << 8));
                if (Best == 3) {
                    if (ScoreOut) {
                        *ScoreOut = Best;
                    }
                    return 1;
                }
            }
        }
        Off = (UINT16)(Off + Len);
    }
    if (ScoreOut) {
        *ScoreOut = Best;
    }
    return Best != 0;
}

static void HidSetBootIdle(UINT8 Addr, UINT8 Iface) {
    UsbSetup Setup;

    Setup.BmRequestType = 0x21;
    Setup.BRequest = 0x0B; /* SET_PROTOCOL boot */
    Setup.WValue = 0;
    Setup.WIndex = Iface;
    Setup.WLength = 0;
    (void)ControlMsg(Addr, &Setup, 0, 0);
    Setup.BRequest = 0x0A; /* SET_IDLE */
    (void)ControlMsg(Addr, &Setup, 0, 0);
}

static UINT8 gNextAddr = 2;

/*
 * 一口一设备：可鼠、可键、可复合。已绑的不再抢；弱键+像样鼠拒认（NUC 同形）。
 * 返回：绑上至少一路 1；无 HID 0；硬失败 -1。
 */
static int EnumHidOnPort(UINT8 Hub, UINT8 Port, UINT16 PortSt) {
    UsbSetup Setup;
    UINT8 Dev[18];
    UINT8 CfgHdr[9];
    UINT8 Cfg[256];
    UINT16 CfgLen;
    UINT8 MIface;
    UINT8 MEp;
    UINT16 MMps;
    UINT8 MScore;
    UINT8 KIface;
    UINT8 KEp;
    UINT16 KMps;
    UINT8 KScore;
    UINT8 Addr;
    int Low;
    int Ret;
    UINT16 Ep0;
    int WantMouse;
    int WantKbd;
    int GotMouse;
    int GotKbd;
    int Bound = 0;

    if ((PortSt & HUB_STAT_HIGH_SPEED) != 0) {
        return 0; /* 板上网口等 HS：非 SPLIT HID */
    }
    WantMouse = !gMouseOk;
    WantKbd = !gKbdOk;
    if (!WantMouse && !WantKbd) {
        return 0;
    }
    /*
     * FS/LS 在 HS hub 后：ep0 首包 MPS 必须 8（USB2 §5.5.3 / 常见 HID）。
     * 曾用 FS→64 先 GET 18 → SPLIT getdesc18 fail（屏上已见）。
     */
    Low = ((PortSt & HUB_STAT_LOW_SPEED) != 0) ? 1 : 0;
    Ep0 = 8;
    XferTargetSplit(Hub, Port, Low, Ep0);

    Setup.BmRequestType = 0x80;
    Setup.BRequest = 6;
    Setup.WValue = 0x0100;
    Setup.WIndex = 0;
    Setup.WLength = 8;
    Ret = ControlMsg(0, &Setup, Dev, 8);
    if (Ret < 8) {
        HalSerialBootMark("Boot: DWC2 SPLIT getdesc8 fail port=");
        Hex16(Port);
        HalSerialBootMark(Low ? " LS\n" : " FS\n");
        return -1;
    }
    Ep0 = Dev[7] ? (UINT16)Dev[7] : 8u;
    if (Ep0 != 8 && Ep0 != 16 && Ep0 != 32 && Ep0 != 64) {
        Ep0 = 8;
    }
    gXferEp0Mps = Ep0;
    /*
     * USB2 §5.5.3：8 字节已够知 MPS，可 SET_ADDR 再取满 18。
     * LS 经 SPLIT 在 addr0 上 GET 18 常 DATATGLERR（录像 hub+3）；
     * 先 SET_ADDR 再 GET 18 是常见稳妥序。
     */
    Setup.WLength = 18;
    Ret = ControlMsg(0, &Setup, Dev, 18);
    if (Ret < 18) {
        HalSerialBootMark("Boot: DWC2 SPLIT getdesc18@0 fail mps=");
        Hex16(Ep0);
        HalSerialBootMark(Low ? " LS → SET_ADDR retry\n" : "\n");
        if (!Low) {
            return -1;
        }
    }

    Addr = gNextAddr++;
    if (gNextAddr > 100) {
        gNextAddr = 2;
    }
    Setup.BmRequestType = 0x00;
    Setup.BRequest = 5;
    Setup.WValue = Addr;
    Setup.WIndex = 0;
    Setup.WLength = 0;
    if (ControlMsg(0, &Setup, 0, 0) < 0) {
        HalSerialBootMark("Boot: DWC2 HID SET_ADDR fail\n");
        return -1;
    }
    Dwc2DelayMs(Low ? 10u : 2u);

    if (Ret < 18) {
        Setup.BmRequestType = 0x80;
        Setup.BRequest = 6;
        Setup.WValue = 0x0100;
        Setup.WIndex = 0;
        Setup.WLength = 18;
        Ret = ControlMsg(Addr, &Setup, Dev, 18);
        if (Ret < 18) {
            HalSerialBootMark("Boot: DWC2 SPLIT getdesc18@addr fail\n");
            return -1;
        }
    }

    HalSerialBootMark("Boot: DWC2 HID vid=");
    Hex16((UINT16)(Dev[8] | ((UINT16)Dev[9] << 8)));
    HalSerialBootMark(" pid=");
    Hex16((UINT16)(Dev[10] | ((UINT16)Dev[11] << 8)));
    HalSerialBootMark(Low ? " LS\n" : " FS\n");

    Setup.BmRequestType = 0x80;
    Setup.BRequest = 6;
    Setup.WValue = 0x0200;
    Setup.WIndex = 0;
    Setup.WLength = 9;
    if (ControlMsg(Addr, &Setup, CfgHdr, 9) < 9) {
        HalSerialBootMark("Boot: DWC2 HID cfg hdr fail\n");
        return -1;
    }
    CfgLen = (UINT16)(CfgHdr[2] | ((UINT16)CfgHdr[3] << 8));
    if (CfgLen < 9 || CfgLen > sizeof(Cfg)) {
        CfgLen = 9;
    }
    Setup.WLength = CfgLen;
    if (ControlMsg(Addr, &Setup, Cfg, (int)CfgLen) < (int)CfgLen) {
        HalSerialBootMark("Boot: DWC2 HID cfg fail\n");
        return -1;
    }

    GotMouse = ParseBootMouse(Cfg, CfgLen, &MIface, &MEp, &MMps, &MScore);
    GotKbd = ParseBootKeyboard(Cfg, CfgLen, &KIface, &KEp, &KMps, &KScore);
    /* 弱键分 + 像样鼠 = 鼠附加 HID，不当键盘（NUC G102 教训） */
    if (GotKbd && KScore < 3 && GotMouse && MScore >= 2) {
        HalSerialBootMark("Boot: DWC2 skip mouse+extraHID as kbd\n");
        GotKbd = 0;
    }
    if (!WantMouse) {
        GotMouse = 0;
    }
    if (!WantKbd) {
        GotKbd = 0;
    }
    if (!GotMouse && !GotKbd) {
        HalSerialBootMark("Boot: DWC2 no boot hid\n");
        return 0;
    }

    Setup.BmRequestType = 0x00;
    Setup.BRequest = 9;
    Setup.WValue = CfgHdr[5] ? CfgHdr[5] : 1;
    Setup.WIndex = 0;
    Setup.WLength = 0;
    if (ControlMsg(Addr, &Setup, 0, 0) < 0) {
        HalSerialBootMark("Boot: DWC2 HID SET_CFG fail\n");
        return -1;
    }

    if (GotMouse) {
        HidSetBootIdle(Addr, MIface);
        gMouseAddr = Addr;
        gMouseEp = (UINT8)(MEp & 0x0Fu);
        gMouseMps = MMps ? MMps : 8;
        gMouseToggle = DWC2_HC_PID_DATA0;
        gMouseHub = Hub;
        gMousePort = Port;
        gMouseSplit = gXferDoSplit;
        gMouseLow = Low;
        gMouseOk = 1;
        Bound = 1;
        HalSerialBootMark("Boot: DWC2 mouse ep=");
        Hex16(gMouseEp);
        HalSerialBootMark(" ok\n");
    }
    if (GotKbd) {
        HidSetBootIdle(Addr, KIface);
        gKbdAddr = Addr;
        gKbdEp = (UINT8)(KEp & 0x0Fu);
        gKbdMps = KMps ? KMps : 8;
        gKbdToggle = DWC2_HC_PID_DATA0;
        gKbdHub = Hub;
        gKbdPort = Port;
        gKbdSplit = gXferDoSplit;
        gKbdLow = Low;
        gKbdOk = 1;
        Bound = 1;
        HalSerialBootMark("Boot: DWC2 kbd ep=");
        Hex16(gKbdEp);
        HalSerialBootMark(" ok\n");
    }
    return Bound;
}

static void MouseEnqueue(UINT8 Btn, INT8 Dx, INT8 Dy, INT8 Wh) {
    UINT8 N = (UINT8)((gMqT + 1u) % MOUSE_Q);
    INT32 X;
    INT32 Y;
    UINT32 Sw = 0;
    UINT32 Sh = 0;
    INT32 MaxX;
    INT32 MaxY;

    if (N == gMqH) {
        gMqH = (UINT8)((gMqH + 1u) % MOUSE_Q);
    }
    /* 同 X64 UsbHid Mouse.c：相对鼠累加到屏像素；Absolute=0 交 Gui */
    HalVideoGetSize(&Sw, &Sh);
    if (Sw == 0) {
        Sw = 1920;
    }
    if (Sh == 0) {
        Sh = 1080;
    }
    MaxX = (INT32)(Sw > 0 ? Sw - 1u : 0u);
    MaxY = (INT32)(Sh > 0 ? Sh - 1u : 0u);
    if (!gMouseAbsInit) {
        gMouseX = Sw / 2u;
        gMouseY = Sh / 2u;
        gMouseAbsInit = 1;
    }
    X = (INT32)gMouseX + (INT32)Dx;
    Y = (INT32)gMouseY + (INT32)Dy;
    if (X < 0) {
        X = 0;
    }
    if (Y < 0) {
        Y = 0;
    }
    if (X > MaxX) {
        X = MaxX;
    }
    if (Y > MaxY) {
        Y = MaxY;
    }
    gMouseX = (UINT32)X;
    gMouseY = (UINT32)Y;
    gMqX[gMqT] = gMouseX;
    gMqY[gMqT] = gMouseY;
    gMqB[gMqT] = Btn;
    gMqW[gMqT] = Wh;
    gMqT = N;
}

static void Dwc2MousePollOnce(void) {
    UINT8 Buf[8];
    int Ret;
    UINT8 Pid;

    if (!gMouseOk) {
        return;
    }
    gXferDoSplit = gMouseSplit;
    gXferLowSpeed = gMouseLow;
    gXferHub = gMouseHub;
    gXferPort = gMousePort;
    Pid = gMouseToggle;
    Ret = ChunkMsg(gMouseAddr, gMouseEp, DWC2_HCCHAR_EPTYPE_INTR, 1, &Pid, Buf,
                   (int)gMouseMps > 8 ? 8 : (int)gMouseMps, gMouseMps);
    if (Ret == -2) {
        return; /* NAK：无报告 */
    }
    if (Ret < 3) {
        return;
    }
    gMouseToggle = Pid;
    MouseEnqueue(Buf[0], (INT8)Buf[1], (INT8)Buf[2],
                 Ret >= 4 ? (INT8)Buf[3] : 0);
}

static void KbdEnqueue(const UINT8 *Rep8) {
    UINT8 N = (UINT8)((gKqT + 1u) % KBD_Q);
    int i;

    if (!Rep8) {
        return;
    }
    if (N == gKqH) {
        return; /* 满：丢新保旧（同 X64 KbdPush） */
    }
    gKq[gKqT].ModifierKeys = Rep8[0];
    gKq[gKqT].Reserved = Rep8[1];
    for (i = 0; i < 6; i++) {
        gKq[gKqT].KeyCode[i] = Rep8[2 + i];
    }
    gKqT = N;
}

static void Dwc2KbdPollOnce(void) {
    UINT8 Buf[8];
    int Ret;
    UINT8 Pid;
    int Len;

    if (!gKbdOk) {
        return;
    }
    gXferDoSplit = gKbdSplit;
    gXferLowSpeed = gKbdLow;
    gXferHub = gKbdHub;
    gXferPort = gKbdPort;
    Pid = gKbdToggle;
    Len = (int)gKbdMps > 8 ? 8 : (int)gKbdMps;
    if (Len < 8) {
        Len = 8; /* boot 报告 8 字节 */
    }
    Ret = ChunkMsg(gKbdAddr, gKbdEp, DWC2_HCCHAR_EPTYPE_INTR, 1, &Pid, Buf, Len,
                   gKbdMps ? gKbdMps : 8);
    if (Ret == -2) {
        return;
    }
    if (Ret < 3) {
        return;
    }
    gKbdToggle = Pid;
    KbdEnqueue(Buf);
}

/* 鼠优先：先鼠后键，勿让键 IN 饿死鼠 */
static void Dwc2HidPoll(void) {
    Dwc2MousePollOnce();
    Dwc2KbdPollOnce();
}

void BoardDwc2MouseHandoff(UINT32 CursorX, UINT32 CursorY) {
    gMouseX = CursorX;
    gMouseY = CursorY;
    gMouseAbsInit = 1;
    gMqH = gMqT; /* 丢积压，避免 Reveal 后飞角 */
}

static int MousePresentWrap(void) {
    return gMouseOk;
}

static int MouseDequeueWrap(HAL_MOUSE_REPORT *Report) {
    if (!Report || gMqH == gMqT) {
        return 0;
    }
    Report->X = gMqX[gMqH];
    Report->Y = gMqY[gMqH];
    Report->Buttons = gMqB[gMqH];
    Report->Wheel = gMqW[gMqH];
    Report->Absolute = 0;
    gMqH = (UINT8)((gMqH + 1u) % MOUSE_Q);
    return 1;
}

static int KeyboardDequeueWrap(HAL_KEYBOARD_REPORT *Report) {
    if (!Report || gKqH == gKqT) {
        return 0;
    }
    *Report = gKq[gKqH];
    gKqH = (UINT8)((gKqH + 1u) % KBD_Q);
    return 1;
}

static const INPUT_BACKEND gDwc2Input = {
    .Poll = Dwc2HidPoll,
    .KeyboardDequeue = KeyboardDequeueWrap,
    .KeyboardSetLeds = 0,
    .MousePresent = MousePresentWrap,
    .MouseDequeue = MouseDequeueWrap,
};

/*
 * 软复位 DWC2 后全员掉线再上电：鼠常先 CONNECT，键（尤其 LS）可能慢几拍。
 * 多轮扫口；已绑成功的口不再 reset（免弄死鼠）。
 */
static UINT8 gPortDone; /* bit P：本口已成功 Enum 过 */

static void HubScanPortsOnce(UINT8 Hub, UINT8 NPorts, UINT8 Pass) {
    UINT8 P;
    UINT16 St;
    UINT16 Ch;

    HalSerialBootMark("Boot: DWC2 hub scan pass=");
    Hex16(Pass);
    HalSerialBootMark("\n");

    for (P = 1; P <= NPorts; P++) {
        if ((gPortDone & (UINT8)(1u << P)) != 0) {
            continue;
        }
        if (gMouseOk && gKbdOk) {
            return;
        }
        XferTargetHub();
        if (HubGetPortStatus(Hub, P, &St, &Ch) != 0) {
            HalSerialBootMark("Boot: DWC2 hub+");
            Hex16(P);
            HalSerialBootMark(" status fail\n");
            continue;
        }
        HalSerialBootMark("Boot: DWC2 hub+");
        Hex16(P);
        HalSerialBootMark(" st=");
        Hex16(St);
        if ((St & HUB_STAT_CONNECT) == 0) {
            HalSerialBootMark("\n");
            continue;
        }
        /* 速度位仅在 Enable 后有效（USB2 §11.24.2.7）→ 先 reset 再判 */
        (void)HubSetPortFeat(Hub, P, HUB_FEAT_C_PORT_CONNECTION, 0);
        if (HubResetPort(Hub, P, &St) != 0) {
            HalSerialBootMark(" rst fail\n");
            continue;
        }
        HalSerialBootMark(" rst=");
        Hex16(St);
        if ((St & HUB_STAT_HIGH_SPEED) != 0) {
            HalSerialBootMark(" HS (skip HID)\n");
            gPortDone |= (UINT8)(1u << P); /* 网口等：勿每轮再 reset */
            continue;
        }
        HalSerialBootMark(St & HUB_STAT_LOW_SPEED ? " LS\n" : " FS\n");
        Dwc2DelayMs(50); /* 口复位恢复；再 SPLIT */
        if (!gMouseOk || !gKbdOk) {
            if (EnumHidOnPort(Hub, P, St) > 0) {
                gPortDone |= (UINT8)(1u << P);
            }
            XferTargetHub();
        }
    }
}

static void HubScanPorts(UINT8 Hub) {
    UsbSetup Setup;
    UINT8 HubDesc[16];
    UINT8 NPorts;
    UINT8 PwrGood;
    UINT8 P;
    UINT8 Pass;
    int Ret;

    XferTargetHub();
    /* SET_CONFIGURATION(1) */
    Setup.BmRequestType = 0x00;
    Setup.BRequest = 9;
    Setup.WValue = 1;
    Setup.WIndex = 0;
    Setup.WLength = 0;
    if (ControlMsg(Hub, &Setup, 0, 0) < 0) {
        HalSerialBootMark("Boot: DWC2 SET_CFG fail\n");
        return;
    }

    /* GET_DESCRIPTOR hub class 0x29 */
    Setup.BmRequestType = 0xA0;
    Setup.BRequest = 6;
    Setup.WValue = 0x2900;
    Setup.WIndex = 0;
    Setup.WLength = 9;
    Ret = ControlMsg(Hub, &Setup, HubDesc, 9);
    if (Ret < 3) {
        HalSerialBootMark("Boot: DWC2 hub-class DESC fail\n");
        return;
    }
    NPorts = HubDesc[2];
    if (NPorts == 0) {
        NPorts = 4;
    }
    if (NPorts > 8) {
        NPorts = 8;
    }
    PwrGood = HubDesc[5];
    if (PwrGood == 0) {
        PwrGood = 50;
    }
    HalSerialBootMark("Boot: DWC2 hub ports=");
    Hex16(NPorts);
    HalSerialBootMark("\n");

    for (P = 1; P <= NPorts; P++) {
        (void)HubSetPortFeat(Hub, P, HUB_FEAT_PORT_POWER, 1);
    }
    /*
     * bPwrOn2PwrGood×2ms（USB2 §11.23.2.1）+ 额外 settle：
     * 我们 CSFTRST 后键常比鼠慢亮 CONNECT（U-Boot 曾用过的键也会重枚）。
     */
    Dwc2DelayMs((UINT32)PwrGood * 2u + 200u);

    gPortDone = 0;
    for (Pass = 1; Pass <= 3; Pass++) {
        HubScanPortsOnce(Hub, NPorts, Pass);
        if (gMouseOk && gKbdOk) {
            break;
        }
        if (Pass < 3) {
            Dwc2DelayMs(300);
        }
    }

    if (gMouseOk || gKbdOk) {
        if (DriverInputAttach(&gDwc2Input) == 0) {
            HalSerialBootMark("Boot: DWC2 DriverInputAttach");
            if (gMouseOk) {
                HalSerialBootMark(" mouse");
            }
            if (gKbdOk) {
                HalSerialBootMark(" kbd");
            }
            HalSerialBootMark("\n");
        } else {
            HalSerialBootMark("Boot: DWC2 DriverInputAttach fail\n");
        }
    }
    if (!gKbdOk) {
        HalSerialBootMark("Boot: DWC2 no kbd after hub passes");
        if (gMouseOk) {
            HalSerialBootMark(" (mouse ok)");
        }
        HalSerialBootMark("\n");
    }
}

static int EnumRootHub(void) {
    UsbSetup Setup;
    UINT8 Desc[18];
    int Ret;
    UINT16 Vid;
    UINT16 Pid;
    UINT8 Cls;
    UINT8 Addr = 1;

    XferTargetHub();
    /* GET_DESCRIPTOR(DEVICE) @ addr0 */
    Setup.BmRequestType = 0x80;
    Setup.BRequest = 6;
    Setup.WValue = 0x0100;
    Setup.WIndex = 0;
    Setup.WLength = 18;
    Ret = ControlMsg(0, &Setup, Desc, 18);
    if (Ret < 8) {
        HalSerialBootMark("Boot: DWC2 GET_DESC fail\n");
        return -1;
    }

    Cls = Desc[4];
    Vid = (UINT16)Desc[8] | ((UINT16)Desc[9] << 8);
    Pid = (UINT16)Desc[10] | ((UINT16)Desc[11] << 8);
    HalSerialBootMark("Boot: DWC2 hub DESC len=");
    Hex16(Desc[0]);
    HalSerialBootMark(" cls=");
    Hex16(Cls);
    HalSerialBootMark(" vid=");
    Hex16(Vid);
    HalSerialBootMark(" pid=");
    Hex16(Pid);
    HalSerialBootMark("\n");
    /* 全 0：多半 HCDMA 未走 0xC0000000 总线别名 */
    if (Desc[0] == 0 || (Vid == 0 && Pid == 0)) {
        HalSerialBootMark("Boot: DWC2 DESC bogus (check DMA bus addr)\n");
        return -1;
    }

    /* SET_ADDRESS */
    Setup.BmRequestType = 0x00;
    Setup.BRequest = 5;
    Setup.WValue = Addr;
    Setup.WIndex = 0;
    Setup.WLength = 0;
    Ret = ControlMsg(0, &Setup, 0, 0);
    if (Ret < 0) {
        HalSerialBootMark("Boot: DWC2 SET_ADDR fail\n");
        return -1;
    }
    Dwc2DelayMs(2);
    gHubAddr = Addr;

    /* GET_DESCRIPTOR again @ new addr（确认控制通路） */
    Setup.BmRequestType = 0x80;
    Setup.BRequest = 6;
    Setup.WValue = 0x0100;
    Setup.WIndex = 0;
    Setup.WLength = 18;
    Ret = ControlMsg(Addr, &Setup, Desc, 18);
    if (Ret < 8) {
        HalSerialBootMark("Boot: DWC2 re-GET fail\n");
        return -1;
    }
    HalSerialBootMark("Boot: DWC2 hub addr=");
    Hex16(Addr);
    HalSerialBootMark(" ok\n");

    HubScanPorts(Addr);
    return 0;
}

int BoardDwc2Probe(void) {
    UINT32 Id;
    UINT32 Hprt;
    UINT32 Gusbcfg;
    UINT32 Spd;

    Id = *Dwc2Reg(DWC2_GSNPSID);
    HalSerialBootMark("Boot: DWC2 GSNPSID=");
    Hex32(Id);
    HalSerialBootMark("\n");

    if ((Id >> 16) != 0x4F54u) {
        HalSerialBootMark("Boot: DWC2 id unexpected\n");
        return -1;
    }

    if (Dwc2WaitBits(DWC2_GRSTCTL, DWC2_GRSTCTL_AHBIDLE, DWC2_GRSTCTL_AHBIDLE,
                     100000u) != 0) {
        HalSerialBootMark("Boot: DWC2 AHB busy\n");
        return -1;
    }
    *Dwc2Reg(DWC2_GRSTCTL) = DWC2_GRSTCTL_CSFTRST;
    if (Dwc2WaitBits(DWC2_GRSTCTL, DWC2_GRSTCTL_CSFTRST, 0, 100000u) != 0) {
        HalSerialBootMark("Boot: DWC2 soft-reset hang\n");
        return -1;
    }
    Dwc2Spin(10000);

    Gusbcfg = *Dwc2Reg(DWC2_GUSBCFG);
    Gusbcfg &= ~DWC2_GUSBCFG_FORCEDEV;
    Gusbcfg |= DWC2_GUSBCFG_FORCEHOST;
    *Dwc2Reg(DWC2_GUSBCFG) = Gusbcfg;
    Dwc2Spin(10000);

    *Dwc2Reg(DWC2_PCGCCTL) = 0;
    *Dwc2Reg(DWC2_GAHBCFG) = DWC2_GAHBCFG_DMAEN | DWC2_GAHBCFG_GLBLINTRMSK;
    /* HS PHY：FSLSPClkSel=30/60MHz（U-Boot init_fslspclksel）；勿留复位后不定值 */
    *Dwc2Reg(DWC2_HCFG) = DWC2_HCFG_FSLSPCLKSEL_30_60_MHZ;

    FlushTxFifo(0x10u);
    FlushRxFifo();
    HaltChannels();

    Hprt = *Dwc2Reg(DWC2_HPRT0);
    Hprt |= DWC2_HPRT0_PRTPWR;
    HprtWrite(Hprt);
    Dwc2DelayMs(100);

    Hprt = *Dwc2Reg(DWC2_HPRT0);
    HalSerialBootMark("Boot: DWC2 HPRT0 pre=");
    Hex32(Hprt);
    HalSerialBootMark("\n");

    if ((Hprt & DWC2_HPRT0_PRTCONNSTS) == 0) {
        HalSerialBootMark("Boot: DWC2 no device on root port\n");
        return 0;
    }

    if (RootPortReset() != 0) {
        HalSerialBootMark("Boot: DWC2 root reset timeout\n");
        Hprt = *Dwc2Reg(DWC2_HPRT0);
        HalSerialBootMark("Boot: DWC2 HPRT0=");
        Hex32(Hprt);
        HalSerialBootMark("\n");
        return -1;
    }

    Hprt = *Dwc2Reg(DWC2_HPRT0);
    Spd = Hprt & DWC2_HPRT0_PRTSPD_MASK;
    gRootSpd = Spd;
    if (Spd == DWC2_HPRT0_PRTSPD_HS) {
        gEp0Mps = 64;
    } else if (Spd == DWC2_HPRT0_PRTSPD_FS) {
        gEp0Mps = 64;
    } else {
        gEp0Mps = 8;
    }

    HalSerialBootMark("Boot: DWC2 HPRT0 post=");
    Hex32(Hprt);
    HalSerialBootMark(Spd == DWC2_HPRT0_PRTSPD_HS ? " HS\n" :
                      Spd == DWC2_HPRT0_PRTSPD_FS ? " FS\n" :
                      Spd == DWC2_HPRT0_PRTSPD_LS ? " LS\n" : " ?\n");

    /* 根口复位后等 hub 醒：对齐 U-Boot dwc2_init_common 1s */
    Dwc2DelayMs(1000);
    if (EnumRootHub() != 0) {
        return -1;
    }
    return 0;
}
