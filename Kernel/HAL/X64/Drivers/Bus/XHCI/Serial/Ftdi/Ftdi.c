/*
 * Ftdi.c — FT232 认领（VID/PID + Bulk OUT + 115200）
 */
#include "Compiler.h"
#include "XHCI/Serial/Ftdi/Private.h"
#include "HalSerial.h"

#define FTDI_VID           0x0403u
#define FTDI_PID_FT232R    0x6001u
#define FTDI_PID_FT232H    0x6014u
#define FTDI_PID_FT231X    0x6015u
#define FTDI_REQ_RESET     0x00u
#define FTDI_REQ_MODEM     0x01u
#define FTDI_REQ_SET_FLOW  0x02u
#define FTDI_REQ_SET_BAUD  0x03u
#define FTDI_REQ_SET_DATA  0x04u
#define FTDI_BAUD_115200   0x001Au
#define FTDI_MODEM_DTR_RTS 0x0303u

UINT32 gFtdiSlot;
UINT32 gFtdiPort;
UINT32 gFtdiRoute;
UINT8 gFtdiHubSlot;
UINT8 gFtdiTtPort;
UINT8 gFtdiSpeed;
UINT32 gFtdiBulkOutDci;
UINT16 gFtdiBulkOutMaxPacketSize;
int gFtdiClaimed;

UINT8 gFtdiDevCtx[2048] BLOCKS_ALIGNED(64);
XHCI_TRB gFtdiEndpoint0Ring[RING_SIZE] BLOCKS_ALIGNED(64);
RING_STATE gFtdiEndpoint0;
XHCI_TRB gFtdiBulkOutRing[RING_SIZE] BLOCKS_ALIGNED(64);
RING_STATE gFtdiBulkOut;
static UINT8 gFtdiConfigBuffer[512] BLOCKS_ALIGNED(64);

static void EndpointZeroRing(XHCI_TRB **RingOut, RING_STATE **StOut) {
    *RingOut = gFtdiEndpoint0Ring;
    *StOut = &gFtdiEndpoint0;
}

static int Ready(void) {
    return (gFtdiClaimed && gFtdiSlot != 0) ? 1 : 0;
}

static int IsFtdiPid(UINT16 Pid) {
    return Pid == FTDI_PID_FT232R || Pid == FTDI_PID_FT232H || Pid == FTDI_PID_FT231X;
}

static int ParseEndpoints(UINT8 *Cfg, UINT16 Total, UINT8 *EndpointIn, UINT16 *MaxPacketSizeIn,
                         UINT8 *EndpointOut, UINT16 *MaxPacketSizeOut) {
    UINT16 Off = 0;
    UINT8 CurInterface = 0xFF;
    UINT8 CurAlt = 0;
    UINT8 BestIn = 0;
    UINT8 BestOut = 0;
    UINT16 BestInMaxPacketSize = 0;
    UINT16 BestOutMaxPacketSize = 0;

    while (Off + 2 <= Total) {
        UINT8 Len = Cfg[Off];
        UINT8 Type = Cfg[Off + 1];

        if (Len < 2 || Off + Len > Total) {
            break;
        }
        if (Type == 4 && Len >= 9) {
            CurInterface = Cfg[Off + 2];
            CurAlt = Cfg[Off + 3];
        } else if (Type == 5 && Len >= 7 && CurInterface != 0xFF && CurAlt == 0) {
            UINT8 Addr = Cfg[Off + 2];
            UINT8 Attr = Cfg[Off + 3];
            UINT16 MaxPacketSize = (UINT16)(Cfg[Off + 4] | (Cfg[Off + 5] << 8));

            if ((Attr & 0x03) == 2) {
                if (Addr & 0x80) {
                    BestIn = Addr;
                    BestInMaxPacketSize = MaxPacketSize ? MaxPacketSize : 64;
                } else {
                    BestOut = Addr;
                    BestOutMaxPacketSize = MaxPacketSize ? MaxPacketSize : 64;
                }
            }
        }
        Off = (UINT16)(Off + Len);
    }
    if (BestIn == 0 || BestOut == 0) {
        return 0;
    }
    if (EndpointIn) {
        *EndpointIn = BestIn;
    }
    if (MaxPacketSizeIn) {
        *MaxPacketSizeIn = BestInMaxPacketSize;
    }
    if (EndpointOut) {
        *EndpointOut = BestOut;
    }
    if (MaxPacketSizeOut) {
        *MaxPacketSizeOut = BestOutMaxPacketSize;
    }
    return 1;
}

static int Vendor(UINT8 Req, UINT16 Value, UINT16 Index) {
    USB_SETUP_PACKET Setup;

    Setup.bmRequestType = 0x40;
    Setup.bRequest = Req;
    Setup.wValue = Value;
    Setup.wIndex = Index;
    Setup.wLength = 0;
    return ControlTransfer(&Setup, 0);
}

static int SetLine(void) {
    if (Vendor(FTDI_REQ_RESET, 0, 0) < 0) {
        return -1;
    }
    if (Vendor(FTDI_REQ_SET_BAUD, FTDI_BAUD_115200, 0) < 0) {
        return -1;
    }
    if (Vendor(FTDI_REQ_SET_DATA, 8, 0) < 0) {
        return -1;
    }
    (void)Vendor(FTDI_REQ_SET_FLOW, 0, 0);
    /* 不拉 DTR/RTS 时，不少转接线 TX 灯亮但 CoolTerm 收不到字节。 */
    (void)Vendor(FTDI_REQ_MODEM, FTDI_MODEM_DTR_RTS, 0);
    return 0;
}

int FtdiFinishAddressed(UINT32 RootPort, UINT8 Speed) {
    UINT8 EndpointIn = 0;
    UINT8 EndpointOut = 0;
    UINT16 MaxPacketSizeIn = 64;
    UINT16 MaxPacketSizeOut = 64;
    UINT16 Total;
    UINT8 ConfigVal = 1;
    UINT16 Vid;
    UINT16 Pid;

    if (gFtdiSlot == 0) {
        return 0;
    }
    gTransferSlot = gFtdiSlot;
    if (GetDeviceDesc() < 0) {
        goto fail;
    }
    Vid = (UINT16)(gControlBuffer[8] | (gControlBuffer[9] << 8));
    Pid = (UINT16)(gControlBuffer[10] | (gControlBuffer[11] << 8));
    if (Vid != FTDI_VID || !IsFtdiPid(Pid)) {
        goto fail;
    }
    if (GetDescriptor(USB_WVALUE_DT_CONFIG, 0, 9, gFtdiConfigBuffer) < 0) {
        goto fail;
    }
    Total = (UINT16)(gFtdiConfigBuffer[2] | (gFtdiConfigBuffer[3] << 8));
    if (Total < 9) {
        Total = 9;
    }
    if (Total > sizeof(gFtdiConfigBuffer)) {
        Total = (UINT16)sizeof(gFtdiConfigBuffer);
    }
    ConfigVal = gFtdiConfigBuffer[5] ? gFtdiConfigBuffer[5] : 1;
    if (GetDescriptor(USB_WVALUE_DT_CONFIG, 0, Total, gFtdiConfigBuffer) < 0) {
        goto fail;
    }
    if (!ParseEndpoints(gFtdiConfigBuffer, Total, &EndpointIn, &MaxPacketSizeIn, &EndpointOut, &MaxPacketSizeOut)) {
        goto fail;
    }
    if (SetConfig(ConfigVal) < 0) {
        goto fail;
    }
    if (!gFtdi.ConfigureBulk(gFtdiSlot, RootPort, Speed, EndpointIn, MaxPacketSizeIn, EndpointOut, MaxPacketSizeOut)) {
        goto fail;
    }
    if (SetLine() < 0) {
        BootLog("Boot: USB-UART Baud Fail\n");
        goto fail;
    }
    gFtdiPort = RootPort;
    gFtdiClaimed = 1;
    gFtdi.ArmReceive();
    HalSerialBootMarkForce("Boot: USB-UART FTDI\n");
    return 1;

fail:
    if (gFtdiSlot != 0) {
        DisableSlot(gFtdiSlot);
        gFtdiSlot = 0;
    }
    return 0;
}

static int AddressRoot(UINT32 P, UINT8 *Speed) {
    UINT32 Ps;
    int Force = 0;

    Ps = ReadMmio32(gOperationalBase + PortRegister(P));
    /*
     * MSC 已占用 hub 时禁止 Force：复位旁路口会拖垮 hub 上的 U 盘 bulk。
     * 仅当口已 PED+CCS 才软 Address（FTDI 已启用过）。
     */
    if (gMscClaimed) {
        if (!(Ps & PORTSC_CCS) || !(Ps & PORTSC_PED)) {
            return 0;
        }
    } else if (!ForceUntilPed(P, &Force)) {
        return 0;
    }
    Ps = ReadMmio32(gOperationalBase + PortRegister(P));
    *Speed = PortSpeed(Ps);
    if (gFtdiSlot != 0) {
        DisableSlot(gFtdiSlot);
        gFtdiSlot = 0;
    }
    gFtdiRoute = 0;
    gFtdiHubSlot = 0;
    gFtdiTtPort = 0;
    if (!AddressDeviceOnPort(P, *Speed, &gFtdiSlot, gFtdiDevCtx, 0, 0, 0, 0, 0)) {
        if (gFtdiSlot != 0) {
            DisableSlot(gFtdiSlot);
            gFtdiSlot = 0;
        }
        return 0;
    }
    if (gFtdiSlot <= DCBAA_SLOTS) {
        gSlotEndpoint0UsesKeyboardRing[gFtdiSlot] = 0;
    }
    return 1;
}

static int Claim(void) {
    UINT32 P;
    UINT8 Speed;

    FtdiPublish();

    if (!gXhciStarted || gOperationalBase == 0 || gMaxPorts == 0) {
        return -1;
    }
    if (gFtdiClaimed && gFtdiSlot != 0) {
        return 1;
    }
    /* usb-hid-keep：MSC sick/扫空 freeze 后勿再 Address */
    if (gXhciPortEnumerateFreeze) {
        BootLog("Boot: FTDI claim skip (port enum freeze, keep mouse)\n");
        return 0;
    }
    if (gHubSlotId != 0 && gFtdi.TryHubChildren && gFtdi.TryHubChildren()) {
        goto armed;
    }
    for (P = 1; P <= gMaxPorts && P <= 32u; P++) {
        UINT32 Ps = ReadMmio32(gOperationalBase + PortRegister(P));

        if (!(Ps & PORTSC_CCS)) {
            continue;
        }
        if (XhciRootPortIsOwned(P)) {
            continue;
        }
        if (!AddressRoot(P, &Speed)) {
            continue;
        }
        if (FtdiFinishAddressed(P, Speed)) {
            goto armed;
        }
    }
    return 0;

armed:
    if (gSlotId != 0 && gIntrDci != 0) {
        QueueIntr();
    }
    if (gMouseSlotId != 0 && gMouseIntrDci != 0) {
        QueueMouseIntr();
    }
    return 1;
}

SERIAL_OPS gFtdi;
USB_UART_OPS gFtdiUart;

void FtdiPublish(void) {
    FtdiFillHub();
    FtdiFillReceive();
    FtdiFillTransmit();
    gFtdi.EndpointZeroRing = EndpointZeroRing;
    gFtdi.FinishClaim = FtdiFinishAddressed;
    gFtdiUart.Claim = Claim;
    gFtdiUart.Ready = Ready;
}
