/*
 * Configure.c — HCI 端点解析 + SetConfig + ConfigEP（btx-2）
 *
 * 接口 E0/01/01：Interrupt IN=事件；Bulk IN/OUT=ACL（本刀架好，不传业务）。
 * 专用环，勿占 HID/MSC 环。
 */
#include "XHCI/Bluetooth/Private.h"

XHCI_TRB gBluetoothEp0Ring[RING_SIZE] BLOCKS_ALIGNED(64);
RING_STATE gBluetoothEp0;
XHCI_TRB gBluetoothEventRing[RING_SIZE] BLOCKS_ALIGNED(64);
RING_STATE gBluetoothEvent;
XHCI_TRB gBluetoothAclInRing[RING_SIZE] BLOCKS_ALIGNED(64);
RING_STATE gBluetoothAclIn;
XHCI_TRB gBluetoothAclOutRing[RING_SIZE] BLOCKS_ALIGNED(64);
RING_STATE gBluetoothAclOut;
UINT8 gBluetoothEventBuffer[BLUETOOTH_EVENT_MAX] BLOCKS_ALIGNED(64);

UINT8 gBluetoothSpeed;
UINT8 gBluetoothInterface;
UINT32 gBluetoothEventDci;
UINT32 gBluetoothAclInDci;
UINT32 gBluetoothAclOutDci;
UINT16 gBluetoothEventMps;
UINT16 gBluetoothAclInMps;
UINT16 gBluetoothAclOutMps;
int gBluetoothConfigured;
int gBluetoothHciResetOk;
volatile UINT32 gBluetoothEventDone;
volatile UINT32 gBluetoothEventCode;

void BluetoothEp0Ring(XHCI_TRB **RingOut, RING_STATE **StateOut) {
    *RingOut = gBluetoothEp0Ring;
    *StateOut = &gBluetoothEp0;
}

static int ParseHciEndpoints(UINT8 *Config, UINT16 Total, UINT8 *IfaceOut,
                             UINT8 *EventEp, UINT16 *EventMps, UINT8 *EventIv,
                             UINT8 *AclInEp, UINT16 *AclInMps, UINT8 *AclOutEp,
                             UINT16 *AclOutMps) {
    UINT16 Offset = 0;
    int InHci = 0;
    UINT8 Iface = 0;
    UINT8 Ev = 0;
    UINT8 In = 0;
    UINT8 Out = 0;
    UINT16 EvMps = 16;
    UINT16 InMps = 64;
    UINT16 OutMps = 64;
    UINT8 EvIv = 1;

    while (Offset + 2u <= Total) {
        UINT8 Length = Config[Offset];
        UINT8 Type;

        if (Length < 2u || (UINT16)(Offset + Length) > Total) {
            break;
        }
        Type = Config[Offset + 1u];
        if (Type == 0x04u && Length >= 9u) {
            InHci = 0;
            if (Config[Offset + 5u] == USB_CLASS_WIRELESS &&
                Config[Offset + 6u] == USB_BLUETOOTH_SUBCLASS &&
                Config[Offset + 7u] == USB_BLUETOOTH_PROTOCOL_HCI) {
                InHci = 1;
                Iface = Config[Offset + 2u];
            }
        } else if (Type == 0x05u && Length >= 7u && InHci) {
            UINT8 Addr = Config[Offset + 2u];
            UINT8 Attr = Config[Offset + 3u] & 0x03u;
            UINT16 Mps = (UINT16)(Config[Offset + 4u] |
                                  (Config[Offset + 5u] << 8));
            UINT8 Iv = Config[Offset + 6u];

            if (Attr == 3u && (Addr & 0x80u) && Ev == 0) {
                Ev = Addr;
                EvMps = Mps ? Mps : 16;
                EvIv = Iv ? Iv : 1;
            } else if (Attr == 2u) {
                if ((Addr & 0x80u) && In == 0) {
                    In = Addr;
                    InMps = Mps ? Mps : 64;
                } else if (!(Addr & 0x80u) && Out == 0) {
                    Out = Addr;
                    OutMps = Mps ? Mps : 64;
                }
            }
        }
        Offset = (UINT16)(Offset + Length);
    }
    if (Ev == 0 || In == 0 || Out == 0) {
        return 0;
    }
    *IfaceOut = Iface;
    *EventEp = Ev;
    *EventMps = EvMps;
    *EventIv = EvIv;
    *AclInEp = In;
    *AclInMps = InMps;
    *AclOutEp = Out;
    *AclOutMps = OutMps;
    return 1;
}

static int ConfigureHciEndpoints(UINT32 SlotId, UINT32 RootPort, UINT8 Speed,
                                 UINT8 EventEp, UINT16 EventMps, UINT8 EventIv,
                                 UINT8 AclInEp, UINT16 AclInMps, UINT8 AclOutEp,
                                 UINT16 AclOutMps) {
    UINT8 EvNum = EventEp & 0x0Fu;
    UINT8 InNum = AclInEp & 0x0Fu;
    UINT8 OutNum = AclOutEp & 0x0Fu;
    UINT32 EvDci = (UINT32)EvNum * 2u + 1u;
    UINT32 InDci = (UINT32)InNum * 2u + 1u;
    UINT32 OutDci = (UINT32)OutNum * 2u;
    UINT32 CtxEntries = EvDci;
    UINT32 *Slot;
    UINT32 *Ep;
    UINT64 Deq;
    UINT8 Interval;

    if (InDci > CtxEntries) {
        CtxEntries = InDci;
    }
    if (OutDci > CtxEntries) {
        CtxEntries = OutDci;
    }
    if (EventMps == 0 || EventMps > BLUETOOTH_EVENT_MAX) {
        EventMps = 16;
    }
    if (AclInMps == 0 || AclInMps > 1024) {
        AclInMps = 64;
    }
    if (AclOutMps == 0 || AclOutMps > 1024) {
        AclOutMps = 64;
    }

    ZeroMemory(gInCtx, sizeof(gInCtx));
    *(UINT32 *)(void *)(gInCtx + 4) =
        (1u << 0) | (1u << EvDci) | (1u << InDci) | (1u << OutDci);

    Slot = (UINT32 *)(void *)InSlot();
    Slot[0] = (CtxEntries << 27) | ((UINT32)Speed << 20);
    Slot[1] = (UINT32)RootPort << 16;

    InitRing(gBluetoothEventRing, &gBluetoothEvent, RING_SIZE);
    InitRing(gBluetoothAclInRing, &gBluetoothAclIn, RING_SIZE);
    InitRing(gBluetoothAclOutRing, &gBluetoothAclOut, RING_SIZE);

    Interval = (Speed >= 3) ? (UINT8)((EventIv > 0) ? (EventIv - 1) : 0)
                            : FsInterval(EventIv);
    Ep = (UINT32 *)(void *)InEp(EvDci);
    Ep[0] = (UINT32)Interval << 16;
    Ep[1] = (3u << 1) | (7u << 3) | ((UINT32)EventMps << 16);
    Deq = PointerToPhysical(gBluetoothEventRing) | 1;
    Ep[2] = (UINT32)Deq;
    Ep[3] = (UINT32)(Deq >> 32);
    Ep[4] = (UINT32)EventMps | ((UINT32)EventMps << 16);

    Ep = (UINT32 *)(void *)InEp(InDci);
    Ep[0] = 0;
    Ep[1] = (3u << 1) | (6u << 3) | ((UINT32)AclInMps << 16);
    Deq = PointerToPhysical(gBluetoothAclInRing) | 1;
    Ep[2] = (UINT32)Deq;
    Ep[3] = (UINT32)(Deq >> 32);
    Ep[4] = (UINT32)AclInMps;

    Ep = (UINT32 *)(void *)InEp(OutDci);
    Ep[0] = 0;
    Ep[1] = (3u << 1) | (2u << 3) | ((UINT32)AclOutMps << 16);
    Deq = PointerToPhysical(gBluetoothAclOutRing) | 1;
    Ep[2] = (UINT32)Deq;
    Ep[3] = (UINT32)(Deq >> 32);
    Ep[4] = (UINT32)AclOutMps;

    FlushDma(gInCtx, sizeof(gInCtx));
    FlushDma(gBluetoothEventRing, sizeof(gBluetoothEventRing));
    FlushDma(gBluetoothAclInRing, sizeof(gBluetoothAclInRing));
    FlushDma(gBluetoothAclOutRing, sizeof(gBluetoothAclOutRing));
    FlushDma(gBluetoothDeviceContext, sizeof(gBluetoothDeviceContext));

    if (Command(PointerToPhysical(gInCtx),
                TRB_TYPE(TRB_CONFIG_EP) | TRB_SLOT(SlotId), 0) < 0) {
        BootLog("Boot: Bluetooth cfg ep fail\n");
        return 0;
    }

    gBluetoothEventDci = EvDci;
    gBluetoothAclInDci = InDci;
    gBluetoothAclOutDci = OutDci;
    gBluetoothEventMps = EventMps;
    gBluetoothAclInMps = AclInMps;
    gBluetoothAclOutMps = AclOutMps;
    return 1;
}

int BluetoothConfigure(UINT32 RootPort, UINT8 Speed) {
    UINT16 Total;
    UINT8 ConfigVal = 1;
    UINT8 Iface = 0;
    UINT8 EventEp = 0;
    UINT8 AclInEp = 0;
    UINT8 AclOutEp = 0;
    UINT16 EventMps = 16;
    UINT16 AclInMps = 64;
    UINT16 AclOutMps = 64;
    UINT8 EventIv = 1;

    if (gBluetoothSlot == 0) {
        return 0;
    }
    gXferSlot = gBluetoothSlot;
    if (GetDesc(USB_WVALUE_DT_CONFIG, 0, 9, gBluetoothConfigBuffer) < 0) {
        BootLog("Boot: Bluetooth cfg9 fail\n");
        return 0;
    }
    Total = (UINT16)(gBluetoothConfigBuffer[2] |
                     (gBluetoothConfigBuffer[3] << 8));
    if (Total < 9u) {
        Total = 9;
    }
    if (Total > BLUETOOTH_CONFIG_MAX) {
        Total = BLUETOOTH_CONFIG_MAX;
    }
    ConfigVal = gBluetoothConfigBuffer[5] ? gBluetoothConfigBuffer[5] : 1;
    if (GetDesc(USB_WVALUE_DT_CONFIG, 0, Total, gBluetoothConfigBuffer) < 0) {
        BootLog("Boot: Bluetooth cfg fail\n");
        return 0;
    }
    if (!ParseHciEndpoints(gBluetoothConfigBuffer, Total, &Iface, &EventEp,
                           &EventMps, &EventIv, &AclInEp, &AclInMps, &AclOutEp,
                           &AclOutMps)) {
        BootLog("Boot: Bluetooth no HCI endpoints\n");
        return 0;
    }
    if (SetConfig(ConfigVal) < 0) {
        BootLog("Boot: Bluetooth setcfg fail\n");
        return 0;
    }
    if (!ConfigureHciEndpoints(gBluetoothSlot, RootPort, Speed, EventEp,
                               EventMps, EventIv, AclInEp, AclInMps, AclOutEp,
                               AclOutMps)) {
        return 0;
    }
    gBluetoothSpeed = Speed;
    gBluetoothInterface = Iface;
    gBluetoothConfigured = 1;
    BootLogHex("Boot: Bluetooth cfg ok iface=", Iface, 2);
    BootLogHex(" event=", EventEp, 2);
    BootLogHex(" acl-in=", AclInEp, 2);
    BootLogHex(" acl-out=", AclOutEp, 2);
    return 1;
}
