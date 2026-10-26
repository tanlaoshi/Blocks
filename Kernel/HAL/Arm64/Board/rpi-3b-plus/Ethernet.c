/*
 * Ethernet.c — Pi3B+ LAN9514 USB 网（rnet-2；对表 U-Boot smsc95xx）
 *
 * L2：vendor REG R/W + bulk TX（8B cmd）/ RX（4B sts）。
 * 鼠优先：Poll 最多一次短 RX；与 HID 共 DWC2 单通道。
 */
#include "Ethernet.h"
#include "UsbHostController.h"
#include "BoardConfig.h"
#include "Hal.h"
#include "Driver.h"
#include "DriverNet.h"
#include "Text.h"
#ifdef LWIP_ENABLE
#include "Netif.h"
#endif

#define SMSC_VID                 0x0424u
#define SMSC_PID_LAN9514_ETH     0xec00u

#define USB_VENDOR_WRITE_REG     0xA0u
#define USB_VENDOR_READ_REG      0xA1u

#define ID_REV                   0x00u
#define INT_STS                  0x08u
#define TX_CFG                   0x10u
#define TX_CFG_ON                0x00000004u
#define HW_CFG                   0x14u
#define HW_CFG_LRST              0x00000008u
#define HW_CFG_BCE               0x00000002u
#define HW_CFG_MEF               0x00000020u
#define HW_CFG_RXDOFF            0x00000600u
#define PM_CTRL                  0x20u
#define PM_CTL_PHY_RST           0x00000010u
#define LED_GPIO_CFG             0x24u
#define LED_GPIO_CFG_LEDS        0x01110000u
#define AFC_CFG                  0x2Cu
#define AFC_CFG_DEFAULT          0x00F830A1u
#define E2P_CMD                  0x30u
#define E2P_CMD_BUSY             0x80000000u
#define E2P_CMD_READ             0x00000000u
#define E2P_CMD_ADDR             0x000001FFu
#define E2P_DATA                 0x34u
#define BURST_CAP                0x38u
#define INT_EP_CTL               0x68u
#define INT_EP_CTL_PHY_INT       0x00008000u
#define BULK_IN_DLY              0x6Cu
#define DEFAULT_BULK_IN_DELAY    0x00002000u
#define MAC_CR                   0x100u
#define MAC_CR_TXEN              0x00000008u
#define MAC_CR_RXEN              0x00000004u
#define MAC_CR_PRMS              0x00040000u
#define MAC_CR_MCPAS             0x00080000u
#define MAC_CR_HPFILT            0x00002000u
#define ADDRH                    0x104u
#define ADDRL                    0x108u
#define MII_ADDR                 0x114u
#define MII_BUSY                 0x01u
#define MII_WRITE                0x02u
#define MII_DATA                 0x118u
#define FLOW                     0x11Cu
#define VLAN1                    0x120u
#define COE_CR                   0x130u

#define TX_CMD_A_FIRST           0x00002000u
#define TX_CMD_A_LAST            0x00001000u
#define RX_STS_FL                0x3FFF0000u
#define RX_STS_ES                0x00008000u

#define SMSC_PHY_ID              1
#define MII_BMSR                 1
#define BMSR_LSTATUS             0x0004u
#define PHY_INT_SRC              29
#define PHY_INT_MASK             30
#define PHY_INT_MASK_DEFAULT     0x0050u

#define RX_URB_SIZE              2048u
#define EEPROM_MAC_OFF           1u

static int gReady;
static int gBound;
static int gLwIpRx;
static UINT8 gAddr;
static UINT8 gHub;
static UINT8 gPort;
static UINT8 gEpIn;
static UINT8 gEpOut;
static UINT16 gBulkMps;
static UINT8 gTxToggle;
static UINT8 gRxToggle;
static UINT32 gMacCr;
static UINT8 gMac[6];
static UINT32 gIp;
static UINT32 gTxDone;
static UINT32 gRxFrames;
static UINT8 gTxBuf[8u + 1518u] __attribute__((aligned(64)));
static UINT8 gRxBuf[RX_URB_SIZE] __attribute__((aligned(64)));

static void DelayMs(UINT32 Ms) {
    UINT32 m;
    volatile UINT32 i;
    for (m = 0; m < Ms; m++) {
        for (i = 0; i < 80000u; i++) {
            __asm__ volatile("nop");
        }
    }
}

static void SelectDev(void) {
    BoardDwc2SelectDevice(gHub, gPort, 0);
}

static int WriteReg(UINT32 Index, UINT32 Data) {
    UINT32 Le = Data;
    int Ret;

    SelectDev();
    Ret = BoardDwc2Control(gAddr, 0x40u, USB_VENDOR_WRITE_REG, 0, (UINT16)Index,
                           &Le, 4);
    return (Ret == 4) ? 0 : -1;
}

static int ReadReg(UINT32 Index, UINT32 *Out) {
    UINT32 Le = 0;
    int Ret;

    if (!Out) {
        return -1;
    }
    SelectDev();
    Ret = BoardDwc2Control(gAddr, 0xC0u, USB_VENDOR_READ_REG, 0, (UINT16)Index,
                           &Le, 4);
    if (Ret != 4) {
        return -1;
    }
    *Out = Le;
    return 0;
}

static int MiiWait(void) {
    UINT32 V;
    UINT32 T;

    for (T = 0; T < 1000u; T++) {
        if (ReadReg(MII_ADDR, &V) != 0) {
            return -1;
        }
        if ((V & MII_BUSY) == 0) {
            return 0;
        }
        DelayMs(1);
    }
    return -1;
}

static int MiiRead(int Idx, UINT16 *Out) {
    UINT32 Addr;
    UINT32 Val;

    if (MiiWait() != 0) {
        return -1;
    }
    Addr = ((UINT32)SMSC_PHY_ID << 11) | ((UINT32)Idx << 6) | MII_BUSY;
    if (WriteReg(MII_ADDR, Addr) != 0 || MiiWait() != 0) {
        return -1;
    }
    if (ReadReg(MII_DATA, &Val) != 0) {
        return -1;
    }
    if (Out) {
        *Out = (UINT16)(Val & 0xFFFFu);
    }
    return 0;
}

static int MiiWrite(int Idx, UINT16 Val) {
    UINT32 Addr;

    if (MiiWait() != 0) {
        return -1;
    }
    if (WriteReg(MII_DATA, Val) != 0) {
        return -1;
    }
    Addr = ((UINT32)SMSC_PHY_ID << 11) | ((UINT32)Idx << 6) | MII_BUSY | MII_WRITE;
    if (WriteReg(MII_ADDR, Addr) != 0) {
        return -1;
    }
    return MiiWait();
}

static int EepromRead(UINT32 Off, UINT8 *Dst, UINT32 Len) {
    UINT32 i;
    UINT32 Cmd;
    UINT32 Data;
    UINT32 T;

    for (i = 0; i < Len; i++) {
        for (T = 0; T < 100u; T++) {
            if (ReadReg(E2P_CMD, &Cmd) != 0) {
                return -1;
            }
            if ((Cmd & E2P_CMD_BUSY) == 0) {
                break;
            }
            DelayMs(1);
        }
        Cmd = E2P_CMD_BUSY | E2P_CMD_READ | ((Off + i) & E2P_CMD_ADDR);
        if (WriteReg(E2P_CMD, Cmd) != 0) {
            return -1;
        }
        for (T = 0; T < 100u; T++) {
            if (ReadReg(E2P_CMD, &Cmd) != 0) {
                return -1;
            }
            if ((Cmd & E2P_CMD_BUSY) == 0) {
                break;
            }
            DelayMs(1);
        }
        if (ReadReg(E2P_DATA, &Data) != 0) {
            return -1;
        }
        Dst[i] = (UINT8)(Data & 0xFFu);
    }
    return 0;
}

static int MacValid(const UINT8 *M) {
    return (M[0] | M[1] | M[2] | M[3] | M[4] | M[5]) != 0 &&
           (M[0] & 1u) == 0;
}

static int InitMac(void) {
    UINT32 Lo;
    UINT32 Hi;
    UINT32 T;
    UINT32 V;

    if (WriteReg(HW_CFG, HW_CFG_LRST) != 0) {
        return -1;
    }
    for (T = 0; T < 100u; T++) {
        if (ReadReg(HW_CFG, &V) != 0) {
            return -1;
        }
        if ((V & HW_CFG_LRST) == 0) {
            break;
        }
        DelayMs(10);
    }
    if (WriteReg(PM_CTRL, PM_CTL_PHY_RST) != 0) {
        return -1;
    }
    for (T = 0; T < 100u; T++) {
        if (ReadReg(PM_CTRL, &V) != 0) {
            return -1;
        }
        if ((V & PM_CTL_PHY_RST) == 0) {
            break;
        }
        DelayMs(10);
    }

    if (EepromRead(EEPROM_MAC_OFF, gMac, 6) != 0 || !MacValid(gMac)) {
        /* U-Boot 无 usbethaddr 时失败；给可辨本地址供课堂 */
        gMac[0] = 0xB8;
        gMac[1] = 0x27;
        gMac[2] = 0xEB;
        gMac[3] = 0x00;
        gMac[4] = 0x00;
        gMac[5] = 0x01;
        HalSerialBootMark("Boot: SMSC MAC fallback\n");
    }

    Lo = (UINT32)gMac[0] | ((UINT32)gMac[1] << 8) | ((UINT32)gMac[2] << 16) |
         ((UINT32)gMac[3] << 24);
    Hi = (UINT32)gMac[4] | ((UINT32)gMac[5] << 8);
    if (WriteReg(ADDRL, Lo) != 0 || WriteReg(ADDRH, Hi) != 0) {
        return -1;
    }

    if (WriteReg(BURST_CAP, 0) != 0) {
        return -1;
    }
    if (WriteReg(BULK_IN_DLY, DEFAULT_BULK_IN_DELAY) != 0) {
        return -1;
    }
    if (ReadReg(HW_CFG, &V) != 0) {
        return -1;
    }
    V &= ~HW_CFG_RXDOFF;
    if (WriteReg(HW_CFG, V) != 0) {
        return -1;
    }
    if (WriteReg(INT_STS, 0xFFFFFFFFu) != 0) {
        return -1;
    }
    if (ReadReg(ID_REV, &V) != 0) {
        return -1;
    }
    HalSerialBootMark("Boot: SMSC ID_REV=");
    {
        char Hex[9];
        HalSerialFormatHex(Hex, V, 8);
        HalSerialBootMark(Hex);
        HalSerialBootMark("\n");
    }
    if (WriteReg(LED_GPIO_CFG, LED_GPIO_CFG_LEDS) != 0) {
        return -1;
    }
    if (WriteReg(FLOW, 0) != 0 || WriteReg(AFC_CFG, AFC_CFG_DEFAULT) != 0) {
        return -1;
    }
    if (ReadReg(MAC_CR, &gMacCr) != 0) {
        return -1;
    }
    if (WriteReg(VLAN1, 0x8100u) != 0) {
        return -1;
    }
    if (WriteReg(COE_CR, 0) != 0) {
        return -1;
    }
    gMacCr &= ~(MAC_CR_PRMS | MAC_CR_MCPAS | MAC_CR_HPFILT);
    if (WriteReg(MAC_CR, gMacCr) != 0) {
        return -1;
    }

    (void)MiiRead(PHY_INT_SRC, 0);
    if (MiiWrite(PHY_INT_MASK, PHY_INT_MASK_DEFAULT) != 0) {
        return -1;
    }
    if (ReadReg(INT_EP_CTL, &V) != 0) {
        return -1;
    }
    V |= INT_EP_CTL_PHY_INT;
    if (WriteReg(INT_EP_CTL, V) != 0) {
        return -1;
    }

    gMacCr |= MAC_CR_TXEN;
    if (WriteReg(MAC_CR, gMacCr) != 0 || WriteReg(TX_CFG, TX_CFG_ON) != 0) {
        return -1;
    }
    gMacCr |= MAC_CR_RXEN;
    if (WriteReg(MAC_CR, gMacCr) != 0) {
        return -1;
    }
    return 0;
}

int BoardEthernetUsbAttach(UINT8 DevAddr, UINT8 Hub, UINT8 Port,
                           UINT8 EpBulkIn, UINT8 EpBulkOut, UINT16 BulkMps) {
    if (gReady) {
        return 0;
    }
    gAddr = DevAddr;
    gHub = Hub;
    gPort = Port;
    gEpIn = EpBulkIn & 0x7Fu;
    gEpOut = EpBulkOut & 0x7Fu;
    gBulkMps = BulkMps ? BulkMps : 512u;
    gTxToggle = 0; /* DATA0 */
    gRxToggle = 0;
    gIp = 0;
    if (InitMac() != 0) {
        HalSerialBootMark("Boot: SMSC init fail\n");
        return -1;
    }
    gReady = 1;
    HalSerialBootMark("Boot: SMSC eth ok mac=");
    {
        static const char Hex[] = "0123456789ABCDEF";
        char B[3];
        int i;
        for (i = 0; i < 6; i++) {
            B[0] = Hex[(gMac[i] >> 4) & 0xF];
            B[1] = Hex[gMac[i] & 0xF];
            B[2] = 0;
            HalSerialBootMark(B);
            if (i < 5) {
                HalSerialBootMark(":");
            }
        }
        HalSerialBootMark("\n");
    }
    return 0;
}

int BoardEthernetSendFrame(const UINT8 *Frame, UINTN FrameLength) {
    UINT32 CmdA;
    UINT32 CmdB;
    int Ret;

    if (!gReady || Frame == 0 || FrameLength < 14u || FrameLength > 1514u) {
        return -1;
    }
    CmdA = (UINT32)FrameLength | TX_CMD_A_FIRST | TX_CMD_A_LAST;
    CmdB = (UINT32)FrameLength;
    gTxBuf[0] = (UINT8)(CmdA);
    gTxBuf[1] = (UINT8)(CmdA >> 8);
    gTxBuf[2] = (UINT8)(CmdA >> 16);
    gTxBuf[3] = (UINT8)(CmdA >> 24);
    gTxBuf[4] = (UINT8)(CmdB);
    gTxBuf[5] = (UINT8)(CmdB >> 8);
    gTxBuf[6] = (UINT8)(CmdB >> 16);
    gTxBuf[7] = (UINT8)(CmdB >> 24);
    {
        UINTN i;
        for (i = 0; i < FrameLength; i++) {
            gTxBuf[8u + i] = Frame[i];
        }
    }
    SelectDev();
    Ret = BoardDwc2Bulk(gAddr, gEpOut, 0, gTxBuf, (int)(8u + FrameLength),
                        gBulkMps, &gTxToggle);
    if (Ret < 0) {
        return -1;
    }
    gTxDone++;
    return 0;
}

static void HandleRx(const UINT8 *Buf, int Len) {
    int Off = 0;

    while (Off + 4 <= Len) {
        UINT32 Sts;
        UINT32 PktLen;
        const UINT8 *Frame;

        Sts = (UINT32)Buf[Off] | ((UINT32)Buf[Off + 1] << 8) |
              ((UINT32)Buf[Off + 2] << 16) | ((UINT32)Buf[Off + 3] << 24);
        Off += 4;
        if (Sts & RX_STS_ES) {
            break;
        }
        PktLen = (Sts & RX_STS_FL) >> 16;
        if (PktLen < 4u || Off + (int)PktLen > Len) {
            break;
        }
        Frame = Buf + Off;
        /* U-Boot：上报长度减 4（FCS） */
        {
            UINTN Fl = PktLen - 4u;
            if (Fl >= 14u) {
                gRxFrames++;
#ifdef LWIP_ENABLE
                if (gLwIpRx) {
                    LwIpNetifInput(Frame, Fl);
                }
#else
                (void)Frame;
#endif
            }
        }
        Off += (int)PktLen;
        if (Off & 3) {
            Off += 4 - (Off & 3);
        }
    }
}

static int EthReady(void) {
    return gReady && gBound;
}

static void EthPoll(void) {
    int Got;

    if (!gReady) {
        return;
    }
    /* 鼠优先：每次最多一次 bulk IN；短超时在 DWC2 层 */
    SelectDev();
    Got = BoardDwc2Bulk(gAddr, gEpIn, 1, gRxBuf, (int)RX_URB_SIZE, gBulkMps,
                        &gRxToggle);
    if (Got > 4) {
        HandleRx(gRxBuf, Got);
    }
}

static void EthGetMac(UINT8 Mac[6]) {
    int i;
    if (!Mac) {
        return;
    }
    for (i = 0; i < 6; i++) {
        Mac[i] = gMac[i];
    }
}

static UINT32 EthGetIp(void) {
    return gIp;
}

static void EthSetIp(UINT32 Ip) {
    gIp = Ip;
}

static void EthFormatIp(UINT32 Ip, char *Buf, int BufLen) {
    if (!Buf || BufLen < 16) {
        return;
    }
    FormatDottedDecimal(Ip, Buf, BufLen);
}

static int EthParseIp(const char *Text, UINT32 *Ip) {
    UINT32 Parts[4];
    int Part = 0;
    UINT32 Val = 0;
    int Digits = 0;

    if (!Text || !Ip) {
        return -1;
    }
    while (*Text) {
        if (*Text >= '0' && *Text <= '9') {
            Val = Val * 10u + (UINT32)(*Text - '0');
            if (Val > 255) {
                return -1;
            }
            Digits++;
        } else if (*Text == '.') {
            if (Digits == 0 || Part >= 3) {
                return -1;
            }
            Parts[Part++] = Val;
            Val = 0;
            Digits = 0;
        } else {
            return -1;
        }
        Text++;
    }
    if (Digits == 0 || Part != 3) {
        return -1;
    }
    Parts[3] = Val;
    *Ip = (Parts[0] << 24) | (Parts[1] << 16) | (Parts[2] << 8) | Parts[3];
    return 0;
}

static UINT16 EthChecksum(const void *Data, UINTN Len) {
    const UINT8 *B = (const UINT8 *)Data;
    UINT32 Sum = 0;
    UINTN i;

    for (i = 0; i + 1 < Len; i += 2) {
        Sum += ((UINT32)B[i] << 8) | B[i + 1];
    }
    if (i < Len) {
        Sum += (UINT32)B[i] << 8;
    }
    while (Sum >> 16) {
        Sum = (Sum & 0xFFFFu) + (Sum >> 16);
    }
    return (UINT16)~Sum;
}

static int EthPing(const char *Host, int TimeoutMs) {
    (void)Host;
    (void)TimeoutMs;
    /* rnet-3：静态 ARP/ICMP */
    return -1;
}

static void EthGetStats(UINT32 *Tx, UINT32 *Rx) {
    if (Tx) {
        *Tx = gTxDone;
    }
    if (Rx) {
        *Rx = gRxFrames;
    }
}

static int EthSendIp(UINT32 Dst, UINT8 Proto, const void *Payload, UINTN Len) {
    (void)Dst;
    (void)Proto;
    (void)Payload;
    (void)Len;
    return -1;
}

static void EthSetLwIpRx(int Enable) {
    gLwIpRx = Enable ? 1 : 0;
}

static const NET_BACKEND gBackend = {
    .Ready = EthReady,
    .Poll = EthPoll,
    .GetMac = EthGetMac,
    .GetIp = EthGetIp,
    .SetIp = EthSetIp,
    .FormatIp = EthFormatIp,
    .ParseIp = EthParseIp,
    .Ping = EthPing,
    .GetStats = EthGetStats,
    .SendIp = EthSendIp,
    .Checksum = EthChecksum,
    .SetLwIpRx = EthSetLwIpRx,
};

static int EthDriverProbe(const DRIVER *Self, void *Bus, void **Out) {
    (void)Self;
    (void)Bus;
    if (!gReady) {
        return -1;
    }
    if (Out) {
        *Out = 0;
    }
    return 0;
}

static int EthDriverBind(DRIVER_INSTANCE *Inst) {
    (void)Inst;
    if (!gReady) {
        return -1;
    }
    if (DriverNetAttach(&gBackend) != 0) {
        return -1;
    }
    gBound = 1;
    HalSerialBootMark("Boot: SMSC ETH bind\n");
    return 0;
}

static void EthDriverRemove(DRIVER_INSTANCE *Inst) {
    (void)Inst;
    gBound = 0;
}

static const DRIVER gEthDriver = {
    .Name = "smsc95xx",
    .Class = DRIVER_CLASS_NET,
    .Match = 0,
    .Probe = EthDriverProbe,
    .Bind = EthDriverBind,
    .Remove = EthDriverRemove,
};

void BoardEthernetRegister(void) {
    (void)DriverRegister(&gEthDriver);
}

int BoardEthernetProbe(void) {
    return gReady ? 0 : -1;
}

int BoardEthernetGetLink(int *Up, UINT32 *Mbps, int *FullDuplex) {
    UINT16 Bmsr = 0;

    if (!gReady) {
        return 0;
    }
    /*
     * IEEE 802.3 BMSR.Link Status 是锁存低：掉线后保持 0 直到读一次。
     * 只读一次会把「曾掉线」当成当前 down（插线仍收包、rx_frames 在涨）。
     * 标准做法：读两次，以第二次为准。
     */
    if (MiiRead(MII_BMSR, &Bmsr) != 0) {
        return 0;
    }
    if (MiiRead(MII_BMSR, &Bmsr) != 0) {
        return 0;
    }
    if (Up) {
        *Up = (Bmsr & BMSR_LSTATUS) ? 1 : 0;
    }
    if (Mbps) {
        *Mbps = 100u;
    }
    if (FullDuplex) {
        *FullDuplex = 1;
    }
    return 1;
}
