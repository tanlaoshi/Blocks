/*
 * Wifi.c — xHCI 上认 RTL8188EU（PR-N-wifi-1）
 *
 * VID 0BDA + PID 白名单；SetConfig；记 Bulk EP 供 wifi-2。不灌固件。
 */
#include "Compiler.h"
#include "XHCI/Wifi/Private.h"
#include "DriverBoot.h"
#include "Rtl8188eu/WifiIds.h"
#include "Rtl8188eu/UsbWifiHost.h"

#define WIFI_CFG_MAX 512

UINT32 gWifiSlot;
UINT32 gWifiPort;
UINT32 gWifiRoute;
UINT8 gWifiHubSlot;
UINT8 gWifiTtPort;
UINT16 gWifiXhciPid;
UINT8 gWifiEndpointIn;
UINT8 gWifiEndpointOut;
UINT16 gWifiMaxPacketSizeIn;
UINT16 gWifiMaxPacketSizeOut;
int gWifiXhciOk;

UINT8 gWifiDevCtx[2048] BLOCKS_ALIGNED(64);
XHCI_TRB gWifiEndpoint0Ring[RING_SIZE] BLOCKS_ALIGNED(64);
RING_STATE gWifiEndpoint0;
static UINT8 gWifiConfigBuffer[WIFI_CFG_MAX] BLOCKS_ALIGNED(64);

void WifiEndpoint0Ring(XHCI_TRB **RingOut, RING_STATE **StOut) {
    *RingOut = gWifiEndpoint0Ring;
    *StOut = &gWifiEndpoint0;
}

int XhciWifiReady(void) {
    return (gWifiXhciOk && gWifiSlot != 0) ? 1 : 0;
}

UINT16 XhciWifiPid(void) {
    return gWifiXhciPid;
}

static int ParseWifiBulk(UINT8 *Cfg, UINT16 Total, UINT8 *EndpointIn, UINT16 *MaxPacketSizeIn,
                         UINT8 *EndpointOut, UINT16 *MaxPacketSizeOut) {
    UINT16 Off = 0;
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
            CurAlt = Cfg[Off + 3];
        } else if (Type == 5 && Len >= 7 && CurAlt == 0) {
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

int WifiFinishClaim(UINT32 RootPort, UINT8 Speed) {
    UINT8 EndpointIn = 0;
    UINT8 EndpointOut = 0;
    UINT16 MaxPacketSizeIn = 64;
    UINT16 MaxPacketSizeOut = 64;
    UINT16 Total;
    UINT8 ConfigVal = 1;
    UINT16 Vid;
    UINT16 Pid;

    (void)Speed;
    if (gWifiSlot == 0) {
        return 0;
    }
    gTransferSlot = gWifiSlot;
    if (GetDeviceDesc() < 0) {
        goto fail;
    }
    Vid = (UINT16)(gControlBuffer[8] | (gControlBuffer[9] << 8));
    Pid = (UINT16)(gControlBuffer[10] | (gControlBuffer[11] << 8));
    if (Vid != WIFI_RTL_VID || !WifiIs8188EuPid(Pid)) {
        goto fail;
    }
    if (GetDescriptor(USB_WVALUE_DT_CONFIG, 0, 9, gWifiConfigBuffer) < 0) {
        goto fail;
    }
    Total = (UINT16)(gWifiConfigBuffer[2] | (gWifiConfigBuffer[3] << 8));
    if (Total < 9) {
        Total = 9;
    }
    if (Total > sizeof(gWifiConfigBuffer)) {
        Total = (UINT16)sizeof(gWifiConfigBuffer);
    }
    ConfigVal = gWifiConfigBuffer[5] ? gWifiConfigBuffer[5] : 1;
    if (GetDescriptor(USB_WVALUE_DT_CONFIG, 0, Total, gWifiConfigBuffer) < 0) {
        goto fail;
    }
    if (!ParseWifiBulk(gWifiConfigBuffer, Total, &EndpointIn, &MaxPacketSizeIn, &EndpointOut, &MaxPacketSizeOut)) {
        goto fail;
    }
    if (SetConfig(ConfigVal) < 0) {
        goto fail;
    }
    gWifiPort = RootPort;
    gWifiEndpointIn = EndpointIn;
    gWifiEndpointOut = EndpointOut;
    gWifiMaxPacketSizeIn = MaxPacketSizeIn;
    gWifiMaxPacketSizeOut = MaxPacketSizeOut;
    gWifiXhciPid = Pid;
    gWifiXhciOk = 1;
    return 1;

fail:
    if (gWifiSlot != 0) {
        DisableSlot(gWifiSlot);
        gWifiSlot = 0;
    }
    return 0;
}

static int WifiAddressRoot(UINT32 P, UINT8 *Speed) {
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
    if (gWifiSlot != 0) {
        DisableSlot(gWifiSlot);
        gWifiSlot = 0;
    }
    gWifiRoute = 0;
    gWifiHubSlot = 0;
    gWifiTtPort = 0;
    if (!AddressDeviceOnPort(P, *Speed, &gWifiSlot, gWifiDevCtx, 0, 0, 0, 0, 0)) {
        if (gWifiSlot != 0) {
            DisableSlot(gWifiSlot);
            gWifiSlot = 0;
        }
        return 0;
    }
    if (gWifiSlot <= DCBAA_SLOTS) {
        gSlotEndpoint0UsesKeyboardRing[gWifiSlot] = 0;
    }
    return 1;
}

int WifiTryHubChildren(void);

int XhciWifiClaim(void) {
    UINT32 P;
    UINT8 Speed;

    if (!gXhciStarted || gOperationalBase == 0 || gMaxPorts == 0) {
        return -1;
    }
    if (gWifiXhciOk && gWifiSlot != 0) {
        return 1;
    }
    /* usb-hid-keep：MSC sick/扫空已 freeze → 勿再 Address（AddressDev 也会拒，此处早退少日志噪声） */
    if (gXhciPortEnumerateFreeze) {
        BootLog("Boot: Wifi claim skip (port enum freeze, keep mouse)\n");
        return 0;
    }
    /*
     * MSC 已认则禁止再扫口/Address：会踩 hub 上 U 盘（cc=0x04 bulk fail）。
     * 主路径已改 iwl PCIe；USB 棒仅无 MSC 时才试。
     */
    if (gMscClaimed) {
        return 0;
    }
    if (gHubSlotId != 0 && WifiTryHubChildren()) {
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
        if (!WifiAddressRoot(P, &Speed)) {
            continue;
        }
        if (WifiFinishClaim(P, Speed)) {
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

void XhciWifiHostOffer(void) {
    static const USB_WIFI_HOST Host = {
        .HostKind = WIFI_HOST_XHCI,
        .Claim = XhciWifiClaim,
        .Pid = XhciWifiPid,
    };

    UsbWifiHostRegister(&Host);
}

DRIVER_BOOT(DRIVER_BOOT_WIFI_XHCI, XhciWifiHostOffer);
