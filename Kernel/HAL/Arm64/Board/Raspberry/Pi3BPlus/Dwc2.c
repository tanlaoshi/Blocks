/*
 * Dwc2.c — Pi3B+ DWC2 host（rpi-7）
 *
 * 本刀：控制传输 + 根口 hub 枚举 + 下游口 power/reset/status（屏打 st=/spd）。
 * 下一刀：SPLIT + HID boot → DriverInputAttach（鼠优先）。
 *
 * 传输形：U-Boot dwc2；hub 类请求对齐 HAL/X64 Hub/Request.c。
 */
#include "Dwc2.h"
#include "Board.h"
#include "Hal.h"
#include "SerialLog.h"

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
                   UINT16 Mps, int LowSpeed) {
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
    *Dwc2Reg(DWC2_HCSPLT(DWC2_HC_CHANNEL)) = 0; /* 根口 HS hub：无 SPLIT */
}

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
    if ((Hcint & (DWC2_HCINT_NAK | DWC2_HCINT_FRMOVRUN)) != 0) {
        return -2; /* EAGAIN */
    }
    HalSerialBootMark("Boot: DWC2 HCINT=");
    Hex32(Hcint);
    HalSerialBootMark("\n");
    return -1;
}

static int TransferChunk(UINT8 *Pid, int In, void *Buffer, UINT32 NumPkts,
                         UINT32 XferLen, UINT32 *ActualLen) {
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
        Hcchar &= ~(DWC2_HCCHAR_CHEN | DWC2_HCCHAR_CHDIS |
                    (3u << DWC2_HCCHAR_MULTICNT_SHIFT));
        Hcchar |= (1u << DWC2_HCCHAR_MULTICNT_SHIFT) | DWC2_HCCHAR_CHEN;
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

/* 根口设备控制/单块（本刀无 SPLIT） */
static int ChunkMsg(UINT8 DevAddr, int In, UINT8 *Pid, void *Buffer, int Len,
                    UINT16 Mps) {
    int Done = 0;
    int Low = (gRootSpd == DWC2_HPRT0_PRTSPD_LS);

    if (Mps == 0) {
        Mps = gEp0Mps;
    }
    HcInit(DevAddr, 0, In, DWC2_HCCHAR_EPTYPE_CTRL, Mps, Low);

    while (Done < Len || Len == 0) {
        UINT32 Xfer;
        UINT32 Npkts;
        UINT32 Actual = 0;
        int Ret;

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

        Ret = TransferChunk(Pid, In,
                            Len ? (void *)((UINT8 *)Buffer + Done) : gStatusBuf,
                            Npkts, Xfer, &Actual);
        if (Ret == -2) {
            return -2;
        }
        if (Ret != 0) {
            return Ret;
        }
        Done += (int)Actual;
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

static int ControlMsg(UINT8 DevAddr, UsbSetup *Setup, void *Data, int Len) {
    UINT8 Pid;
    int Ret;
    int Act = 0;
    int StatusIn;
    int Try;

    /* SETUP */
    for (Try = 0; Try < (int)DWC2_CTRL_RETRY; Try++) {
        Pid = DWC2_HC_PID_SETUP;
        {
            UINT32 i;
            for (i = 0; i < 8; i++) {
                gSetupBuf[i] = ((UINT8 *)Setup)[i];
            }
        }
        Ret = ChunkMsg(DevAddr, 0, &Pid, gSetupBuf, 8, gEp0Mps);
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

    /* DATA */
    if (Data != 0 && Len > 0) {
        int In = (Setup->BmRequestType & 0x80u) != 0;
        for (Try = 0; Try < (int)DWC2_CTRL_RETRY; Try++) {
            Pid = DWC2_HC_PID_DATA1;
            Ret = ChunkMsg(DevAddr, In, &Pid, Data, Len, gEp0Mps);
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

    /* STATUS */
    for (Try = 0; Try < (int)DWC2_CTRL_RETRY; Try++) {
        Pid = DWC2_HC_PID_DATA1;
        Ret = ChunkMsg(DevAddr, StatusIn, &Pid, gStatusBuf, 0, gEp0Mps);
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

static void HubScanPorts(UINT8 Hub) {
    UsbSetup Setup;
    UINT8 HubDesc[16];
    UINT8 NPorts;
    UINT8 PwrGood;
    UINT8 P;
    int Ret;
    UINT16 St;
    UINT16 Ch;

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
    /* bPwrOn2PwrGood 单位 2ms（USB2 §11.23.2.1） */
    Dwc2DelayMs((UINT32)PwrGood * 2u + 50u);

    for (P = 1; P <= NPorts; P++) {
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
        (void)HubSetPortFeat(Hub, P, HUB_FEAT_C_PORT_CONNECTION, 0);
        if (HubResetPort(Hub, P, &St) != 0) {
            HalSerialBootMark(" rst fail\n");
            continue;
        }
        HalSerialBootMark(St & HUB_STAT_HIGH_SPEED ? " HS" :
                          St & HUB_STAT_LOW_SPEED ? " LS" : " FS");
        HalSerialBootMark(" (SPLIT next)\n");
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
    *Dwc2Reg(DWC2_HCFG) = 0;

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

    /* 根口上电后稍等再枚举（U-Boot 给 1s；此处 200ms 够 hub 醒） */
    Dwc2DelayMs(200);
    if (EnumRootHub() != 0) {
        return -1;
    }
    return 0;
}
