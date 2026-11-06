/* Cdc.c — CDC-ACM 认领（Comm 0x02/02/01 + Data 0x0A） */
#include "Compiler.h"
#include "XHCI/Private.h"
#include "XHCI/Serial/Cdc/Private.h"
UINT32 gCdcSlot;
UINT32 gCdcPort;
UINT32 gCdcRoute;
UINT8 gCdcHubSlot;
UINT8 gCdcTtPort;
UINT8 gCdcCommInterface;
UINT32 gCdcBulkOutDci;
UINT16 gCdcBulkOutMaxPacketSize;
UINT32 gCdcBulkInDci;
UINT16 gCdcBulkInMaxPacketSize;
int gCdcClaimed;

UINT8 gCdcDevCtx[2048] BLOCKS_ALIGNED(64);
XHCI_TRB gCdcEndpoint0Ring[RING_SIZE] BLOCKS_ALIGNED(64);
RING_STATE gCdcEndpoint0;
XHCI_TRB gCdcBulkOutRing[RING_SIZE] BLOCKS_ALIGNED(64);
RING_STATE gCdcBulkOut;
XHCI_TRB gCdcBulkInRing[RING_SIZE] BLOCKS_ALIGNED(64);
RING_STATE gCdcBulkIn;
static UINT8 gCdcConfigBuffer[512] BLOCKS_ALIGNED(64);
static UINT8 gCdcLineCoding[7] BLOCKS_ALIGNED(64);

static void EndpointZeroRing(XHCI_TRB **RingOut, RING_STATE **StOut) {
    *RingOut = gCdcEndpoint0Ring;
    *StOut = &gCdcEndpoint0;
}

static int Ready(void) {
    return (gCdcClaimed && gCdcSlot != 0) ? 1 : 0;
}

/* Comm iface + Data Bulk IN/OUT；不对则 0 */
static int ParseAcm(UINT8 *Cfg, UINT16 Total, UINT8 *CommInterface,
                       UINT8 *EndpointIn, UINT16 *MaxPacketSizeIn, UINT8 *EndpointOut, UINT16 *MaxPacketSizeOut) {
    UINT16 Off = 0;
    UINT8 CurInterface = 0xFF;
    UINT8 CurAlt = 0;
    UINT8 CurClass = 0;
    UINT8 CurSub = 0;
    UINT8 CurProto = 0;
    UINT8 BestComm = 0xFF;
    UINT8 DataInterface = 0xFF;
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
            CurClass = Cfg[Off + 5];
            CurSub = Cfg[Off + 6];
            CurProto = Cfg[Off + 7];
            if (CurClass == 0x02 && CurSub == 0x02 && CurProto == 0x01 &&
                BestComm == 0xFF) {
                BestComm = CurInterface;
            }
            if (CurClass == 0x0A && DataInterface == 0xFF) {
                DataInterface = CurInterface;
            }
        } else if (Type == 5 && Len >= 7 && CurInterface != 0xFF && CurAlt == 0 &&
                   CurInterface == DataInterface && CurClass == 0x0A) {
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
    if (BestComm == 0xFF || BestIn == 0 || BestOut == 0) {
        return 0;
    }
    if (CommInterface) {
        *CommInterface = BestComm;
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

static int SetLine(UINT8 CommInterface) {
    USB_SETUP_PACKET Setup;

    /* dwDTERate=115200 LE, 1 stop, no parity, 8 bits */
    gCdcLineCoding[0] = 0x00;
    gCdcLineCoding[1] = 0xC2;
    gCdcLineCoding[2] = 0x01;
    gCdcLineCoding[3] = 0x00;
    gCdcLineCoding[4] = 0;
    gCdcLineCoding[5] = 0;
    gCdcLineCoding[6] = 8;
    Setup.bmRequestType = 0x21;
    Setup.bRequest = 0x20; /* SET_LINE_CODING */
    Setup.wValue = 0;
    Setup.wIndex = CommInterface;
    Setup.wLength = 7;
    if (ControlTransfer(&Setup, gCdcLineCoding) < 0) {
        return -1;
    }
    Setup.bRequest = 0x22; /* SET_CONTROL_LINE_STATE */
    Setup.wValue = USB_CDC_DTR_RTS; /* DTR|RTS */
    Setup.wLength = 0;
    (void)ControlTransfer(&Setup, 0);
    return 0;
}

static int Finish(UINT32 RootPort, UINT8 Speed) {
    UINT8 EndpointIn = 0;
    UINT8 EndpointOut = 0;
    UINT8 Comm = 0;
    UINT16 MaxPacketSizeIn = 64;
    UINT16 MaxPacketSizeOut = 64;
    UINT16 Total;
    UINT8 ConfigVal = 1;

    if (gCdcSlot == 0) {
        return 0;
    }
    gTransferSlot = gCdcSlot;
    if (GetDeviceDesc() < 0) {
        goto fail;
    }
    if (GetDescriptor(USB_WVALUE_DT_CONFIG, 0, 9, gCdcConfigBuffer) < 0) {
        goto fail;
    }
    Total = (UINT16)(gCdcConfigBuffer[2] | (gCdcConfigBuffer[3] << 8));
    if (Total < 9) {
        Total = 9;
    }
    if (Total > sizeof(gCdcConfigBuffer)) {
        Total = (UINT16)sizeof(gCdcConfigBuffer);
    }
    ConfigVal = gCdcConfigBuffer[5] ? gCdcConfigBuffer[5] : 1;
    if (GetDescriptor(USB_WVALUE_DT_CONFIG, 0, Total, gCdcConfigBuffer) < 0) {
        goto fail;
    }
    if (!ParseAcm(gCdcConfigBuffer, Total, &Comm, &EndpointIn, &MaxPacketSizeIn, &EndpointOut, &MaxPacketSizeOut)) {
        goto fail;
    }
    if (SetConfig(ConfigVal) < 0) {
        goto fail;
    }
    gCdcCommInterface = Comm;
    if (!gCdc.ConfigureBulk(gCdcSlot, RootPort, Speed, EndpointIn, MaxPacketSizeIn, EndpointOut, MaxPacketSizeOut)) {
        goto fail;
    }
    if (SetLine(Comm) < 0) {
        BootLog("Boot: USB-UART CDC Line Fail\n");
        goto fail;
    }
    /* QEMU usb-serial：Config/Line 后需短暂就绪，否则首包 Bulk OUT 易超时 */
    if (HalCpuIsHypervisor()) {
        StallMilliseconds(20);
    } else {
        StallMilliseconds(50);
    }
    gCdcPort = RootPort;
    gCdcClaimed = 1;
    gCdc.ArmReceive();
    /* COM1 + CDC 双路；BootMark 保证 QEMU 非 verbose 也看得见 */
    LogBoot("Boot: USB-UART CDC\n");
    BootMarkUsb("Boot: USB-UART CDC\n");
    return 1;

fail:
    if (gCdcSlot != 0) {
        DisableSlot(gCdcSlot);
        gCdcSlot = 0;
    }
    return 0;
}
static int AddressRoot(UINT32 P, UINT8 *Speed) {
    UINT32 Ps;
    int Force = 0;

    Ps = ReadMmio32(gOperationalBase + PortRegister(P));
    if (gMscClaimed) {
        if (!(Ps & PORTSC_CCS) || !(Ps & PORTSC_PED)) {
            return 0;
        }
    } else if (!ForceUntilPed(P, &Force)) {
        return 0;
    }
    Ps = ReadMmio32(gOperationalBase + PortRegister(P));
    *Speed = PortSpeed(Ps);
    if (gCdcSlot != 0) {
        DisableSlot(gCdcSlot);
        gCdcSlot = 0;
    }
    gCdcRoute = 0;
    gCdcHubSlot = 0;
    gCdcTtPort = 0;
    if (!AddressDeviceOnPort(P, *Speed, &gCdcSlot, gCdcDevCtx, 0, 0, 0, 0, 0)) {
        if (gCdcSlot != 0) {
            DisableSlot(gCdcSlot);
            gCdcSlot = 0;
        }
        return 0;
    }
    if (gCdcSlot <= DCBAA_SLOTS) {
        gSlotEndpoint0UsesKeyboardRing[gCdcSlot] = 0;
    }
    return 1;
}

static int Claim(void) {
    UINT32 P;
    UINT8 Speed;

    CdcPublish();

    if (!gXhciStarted || gOperationalBase == 0 || gMaxPorts == 0) {
        return -1;
    }
    if (gFtdiClaimed) {
        return 0;
    }
    if (gCdcClaimed && gCdcSlot != 0) {
        return 1;
    }
    /* usb-hid-keep：MSC sick/扫空 freeze 后勿再 Address */
    if (gXhciPortEnumerateFreeze) {
        BootLog("Boot: CDC claim skip (port enum freeze, keep mouse)\n");
        return 0;
    }

    for (P = 1; P <= gMaxPorts && P <= 32u; P++) {
        UINT32 Ps = ReadMmio32(gOperationalBase + PortRegister(P));

        if (!(Ps & PORTSC_CCS)) {
            continue;
        }
        if (gSlotId != 0 && P == gPort1) {
            continue;
        }
        if (gMouseSlotId != 0 && P == gMousePort) {
            continue;
        }
        if (gHubSlotId != 0 && P == gHubRootPort) {
            continue;
        }
        if (gMscClaimed && gMscPort == P) {
            continue;
        }
        if (gFtdiClaimed && gFtdiPort == P) {
            continue;
        }
        if (!AddressRoot(P, &Speed)) {
            continue;
        }
        if (Finish(P, Speed)) {
            if (gSlotId != 0 && gIntrDci != 0) {
                QueueIntr();
            }
            if (gMouseSlotId != 0 && gMouseIntrDci != 0) {
                QueueMouseIntr();
            }
            return 1;
        }
    }
    return 0;
}

SERIAL_OPS gCdc;
USB_UART_OPS gCdcUart;

void CdcPublish(void) {
    CdcFillReceive();
    CdcFillTransmit();
    gCdc.EndpointZeroRing = EndpointZeroRing;
    gCdc.FinishClaim = Finish;
    gCdcUart.Claim = Claim;
    gCdcUart.Ready = Ready;
}
