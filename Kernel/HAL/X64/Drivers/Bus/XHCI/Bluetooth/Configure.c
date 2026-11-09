/*
 * Configure.c — HCI 端点解析 + SetConfig + ConfigEP（btx-2）
 *
 * 接口 E0/01/01：Interrupt IN=事件；Bulk IN/OUT=ACL（本刀架好，不传业务）。
 * 专用环，勿占 HID/MSC 环。
 */
#include "XHCI/Bluetooth/Private.h"

XHCI_TRB gBluetoothEndpoint0Ring[RING_SIZE] BLOCKS_ALIGNED(64);
RING_STATE gBluetoothEndpoint0;
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
UINT16 gBluetoothEventMaxPacketSize;
UINT16 gBluetoothAclInMaxPacketSize;
UINT16 gBluetoothAclOutMaxPacketSize;
int gBluetoothConfigured;
int gBluetoothHciResetOk;
volatile UINT32 gBluetoothEventDone;
volatile UINT32 gBluetoothEventCode;
volatile UINT32 gBluetoothAclInDone;
volatile UINT32 gBluetoothAclInCode;
volatile UINT32 gBluetoothAclOutDone;
volatile UINT32 gBluetoothAclOutCode;

void BluetoothEndpoint0Ring(XHCI_TRB **RingOut, RING_STATE **StateOut) {
    *RingOut = gBluetoothEndpoint0Ring;
    *StateOut = &gBluetoothEndpoint0;
}

static int ParseHciEndpoints(UINT8 *Config, UINT16 Total, UINT8 *InterfaceOut,
                             UINT8 *EventEndpoint, UINT16 *EventMaxPacketSize, UINT8 *EventIv,
                             UINT8 *AclInEndpoint, UINT16 *AclInMaxPacketSize, UINT8 *AclOutEndpoint,
                             UINT16 *AclOutMaxPacketSize) {
    UINT16 Offset = 0;
    int InHci = 0;
    UINT8 Interface = 0;
    UINT8 Ev = 0;
    UINT8 In = 0;
    UINT8 Out = 0;
    UINT16 EvMaxPacketSize = 16;
    UINT16 InMaxPacketSize = 64;
    UINT16 OutMaxPacketSize = 64;
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
                Interface = Config[Offset + 2u];
            }
        } else if (Type == 0x05u && Length >= 7u && InHci) {
            UINT8 Addr = Config[Offset + 2u];
            UINT8 Attr = Config[Offset + 3u] & 0x03u;
            UINT16 MaxPacketSize = (UINT16)(Config[Offset + 4u] |
                                  (Config[Offset + 5u] << 8));
            UINT8 Iv = Config[Offset + 6u];

            if (Attr == 3u && (Addr & 0x80u) && Ev == 0) {
                Ev = Addr;
                EvMaxPacketSize = MaxPacketSize ? MaxPacketSize : 16;
                EvIv = Iv ? Iv : 1;
            } else if (Attr == 2u) {
                if ((Addr & 0x80u) && In == 0) {
                    In = Addr;
                    InMaxPacketSize = MaxPacketSize ? MaxPacketSize : 64;
                } else if (!(Addr & 0x80u) && Out == 0) {
                    Out = Addr;
                    OutMaxPacketSize = MaxPacketSize ? MaxPacketSize : 64;
                }
            }
        }
        Offset = (UINT16)(Offset + Length);
    }
    if (Ev == 0 || In == 0 || Out == 0) {
        return 0;
    }
    *InterfaceOut = Interface;
    *EventEndpoint = Ev;
    *EventMaxPacketSize = EvMaxPacketSize;
    *EventIv = EvIv;
    *AclInEndpoint = In;
    *AclInMaxPacketSize = InMaxPacketSize;
    *AclOutEndpoint = Out;
    *AclOutMaxPacketSize = OutMaxPacketSize;
    return 1;
}

static int ConfigureHciEndpoints(UINT32 SlotId, UINT32 RootPort, UINT8 Speed,
                                 UINT8 EventEndpoint, UINT16 EventMaxPacketSize, UINT8 EventIv,
                                 UINT8 AclInEndpoint, UINT16 AclInMaxPacketSize, UINT8 AclOutEndpoint,
                                 UINT16 AclOutMaxPacketSize) {
    UINT8 EvNum = EventEndpoint & 0x0Fu;
    UINT8 InNum = AclInEndpoint & 0x0Fu;
    UINT8 OutNum = AclOutEndpoint & 0x0Fu;
    UINT32 EvDci = (UINT32)EvNum * 2u + 1u;
    UINT32 InDci = (UINT32)InNum * 2u + 1u;
    UINT32 OutDci = (UINT32)OutNum * 2u;
    UINT32 CtxEntries = EvDci;
    UINT32 *Slot;
    UINT32 *Endpoint;
    UINT64 Deq;
    UINT8 Interval;

    /*
     * Event + ACL IN + ACL OUT 全配。
     * bootloader 0xFC09：命令 Bulk OUT、事件 Bulk IN（对表 btusb）；
     * 常态 HCI：命令 Control、事件 Interrupt IN。
     */
    if (InDci > CtxEntries) {
        CtxEntries = InDci;
    }
    if (OutDci > CtxEntries) {
        CtxEntries = OutDci;
    }
    if (EventMaxPacketSize == 0 || EventMaxPacketSize > BLUETOOTH_EVENT_MAX) {
        EventMaxPacketSize = 16;
    }
    if (AclInMaxPacketSize == 0 || AclInMaxPacketSize > 1024) {
        AclInMaxPacketSize = 64;
    }
    if (AclOutMaxPacketSize == 0 || AclOutMaxPacketSize > 1024) {
        AclOutMaxPacketSize = 64;
    }

    ZeroMemory(gInCtx, sizeof(gInCtx));
    *(UINT32 *)(void *)(gInCtx + 4) =
        (1u << 0) | (1u << EvDci) | (1u << InDci) | (1u << OutDci);

    Slot = (UINT32 *)(void *)InSlot();
    Slot[0] = (CtxEntries << 27) | ((UINT32)Speed << 20);
    Slot[1] = (UINT32)RootPort << 16;

    RingInitialize(gBluetoothEventRing, &gBluetoothEvent, RING_SIZE);
    RingInitialize(gBluetoothAclInRing, &gBluetoothAclIn, RING_SIZE);
    RingInitialize(gBluetoothAclOutRing, &gBluetoothAclOut, RING_SIZE);

    Interval = (Speed >= 3) ? (UINT8)((EventIv > 0) ? (EventIv - 1) : 0)
                            : FsInterval(EventIv);
    Endpoint = (UINT32 *)(void *)InEndpoint(EvDci);
    Endpoint[0] = (UINT32)Interval << 16;
    Endpoint[1] = (3u << 1) | (7u << 3) | ((UINT32)EventMaxPacketSize << 16);
    Deq = PointerToPhysical(gBluetoothEventRing) | 1;
    Endpoint[2] = (UINT32)Deq;
    Endpoint[3] = (UINT32)(Deq >> 32);
    Endpoint[4] = (UINT32)EventMaxPacketSize | ((UINT32)EventMaxPacketSize << 16);

    Endpoint = (UINT32 *)(void *)InEndpoint(InDci);
    Endpoint[0] = 0;
    Endpoint[1] = (3u << 1) | (6u << 3) | ((UINT32)AclInMaxPacketSize << 16);
    Deq = PointerToPhysical(gBluetoothAclInRing) | 1;
    Endpoint[2] = (UINT32)Deq;
    Endpoint[3] = (UINT32)(Deq >> 32);
    Endpoint[4] = (UINT32)AclInMaxPacketSize;

    Endpoint = (UINT32 *)(void *)InEndpoint(OutDci);
    Endpoint[0] = 0;
    Endpoint[1] = (3u << 1) | (2u << 3) | ((UINT32)AclOutMaxPacketSize << 16);
    Deq = PointerToPhysical(gBluetoothAclOutRing) | 1;
    Endpoint[2] = (UINT32)Deq;
    Endpoint[3] = (UINT32)(Deq >> 32);
    Endpoint[4] = (UINT32)AclOutMaxPacketSize;

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
    gBluetoothEventMaxPacketSize = EventMaxPacketSize;
    gBluetoothAclInMaxPacketSize = AclInMaxPacketSize;
    gBluetoothAclOutMaxPacketSize = AclOutMaxPacketSize;
    BootLogHex("Boot: Bluetooth ep speed=", Speed, 2);
    BootLogHex(" event-dci=", EvDci, 2);
    BootLogHex(" interval=", Interval, 2);
    BootLogHex(" event-mps=", EventMaxPacketSize, 4);
    return 1;
}

int BluetoothConfigure(UINT32 RootPort, UINT8 Speed) {
    UINT16 Total;
    UINT8 ConfigVal = 1;
    UINT8 Interface = 0;
    UINT8 EventEndpoint = 0;
    UINT8 AclInEndpoint = 0;
    UINT8 AclOutEndpoint = 0;
    UINT16 EventMaxPacketSize = 16;
    UINT16 AclInMaxPacketSize = 64;
    UINT16 AclOutMaxPacketSize = 64;
    UINT8 EventIv = 1;

    if (gBluetoothSlot == 0) {
        return 0;
    }
    gTransferSlot = gBluetoothSlot;
    if (GetDescriptor(USB_WVALUE_DT_CONFIG, 0, 9, gBluetoothConfigBuffer) < 0) {
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
    if (GetDescriptor(USB_WVALUE_DT_CONFIG, 0, Total, gBluetoothConfigBuffer) < 0) {
        BootLog("Boot: Bluetooth cfg fail\n");
        return 0;
    }
    if (!ParseHciEndpoints(gBluetoothConfigBuffer, Total, &Interface, &EventEndpoint,
                           &EventMaxPacketSize, &EventIv, &AclInEndpoint, &AclInMaxPacketSize, &AclOutEndpoint,
                           &AclOutMaxPacketSize)) {
        BootLog("Boot: Bluetooth no HCI endpoints\n");
        return 0;
    }
    if (SetConfig(ConfigVal) < 0) {
        BootLog("Boot: Bluetooth setcfg fail\n");
        return 0;
    }
    /* 对表 EnumeratePort HID：复合 iface 显式 SET_INTERFACE(alt=0) */
    if (SetInterface(Interface, 0) < 0) {
        BootLogHex("Boot: Bluetooth setiface fail iface=", Interface, 2);
        return 0;
    }
    if (!ConfigureHciEndpoints(gBluetoothSlot, RootPort, Speed, EventEndpoint,
                               EventMaxPacketSize, EventIv, AclInEndpoint, AclInMaxPacketSize, AclOutEndpoint,
                               AclOutMaxPacketSize)) {
        return 0;
    }
    gBluetoothSpeed = Speed;
    gBluetoothInterface = Interface;
    gBluetoothConfigured = 1;
    BootLogHex("Boot: Bluetooth cfg ok iface=", Interface, 2);
    BootLogHex(" event=", EventEndpoint, 2);
    BootLogHex(" acl-in=", AclInEndpoint, 2);
    BootLogHex(" acl-out=", AclOutEndpoint, 2);
    return 1;
}
