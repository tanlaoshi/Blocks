/*
 * Probe.c — xHCI 上认无线控制器 / 蓝牙 HCI（btx-1）
 *
 * USB Class 0xE0（Wireless）或接口 E0/01/01（Bluetooth HCI）。
 * 只软 Address + 读描述符 + Boot 日志；立刻 DisableSlot。
 * 不 SetConfig、不配端点、不 HCI Reset（btx-2）。
 * 禁止 ForcePR；禁止进 Worker/HotPoll。
 *
 * 调用点：UsbInitialize 键鼠后 → HalBluetoothInitialize（HC 已起）。
 */
#include "Compiler.h"
#include "XHCI/Private.h"
#include "XHCI.h"

#define BLUETOOTH_CONFIG_MAX 512
#define USB_CLASS_WIRELESS 0xE0u
#define USB_BLUETOOTH_SUBCLASS 0x01u
#define USB_BLUETOOTH_PROTOCOL_HCI 0x01u

static UINT8 gBluetoothDeviceContext[2048] BLOCKS_ALIGNED(64);
static UINT8 gBluetoothConfigBuffer[BLUETOOTH_CONFIG_MAX] BLOCKS_ALIGNED(64);
static UINT32 gBluetoothSlot;
static int gBluetoothUsbSeen;
static UINT16 gBluetoothVendorId;
static UINT16 gBluetoothProductId;
static UINT32 gBluetoothRootPort;
static int gBluetoothProbeDone;

int XhciBluetoothSeen(void) {
    return gBluetoothUsbSeen ? 1 : 0;
}

UINT16 XhciBluetoothVendorId(void) {
    return gBluetoothVendorId;
}

UINT16 XhciBluetoothProductId(void) {
    return gBluetoothProductId;
}

UINT32 XhciBluetoothRootPort(void) {
    return gBluetoothUsbSeen ? gBluetoothRootPort : 0;
}

static void BluetoothRequeueHid(void) {
    if (gSlotId != 0 && gIntrDci != 0) {
        QueueIntr();
    }
    if (gMouseSlotId != 0 && gMouseIntrDci != 0) {
        QueueMouseIntr();
    }
}

static void BluetoothReleaseSlot(void) {
    if (gBluetoothSlot != 0) {
        DisableSlot(gBluetoothSlot);
        gBluetoothSlot = 0;
    }
}

static int BluetoothConfigHasHci(UINT8 *Config, UINT16 Total) {
    UINT16 Offset = 0;

    while (Offset + 2u <= Total) {
        UINT8 Length = Config[Offset];
        UINT8 Type;

        if (Length < 2u || (UINT16)(Offset + Length) > Total) {
            break;
        }
        Type = Config[Offset + 1u];
        if (Type == 0x04u && Length >= 9u) {
            if (Config[Offset + 5u] == USB_CLASS_WIRELESS &&
                Config[Offset + 6u] == USB_BLUETOOTH_SUBCLASS &&
                Config[Offset + 7u] == USB_BLUETOOTH_PROTOCOL_HCI) {
                return 1;
            }
        }
        Offset = (UINT16)(Offset + Length);
    }
    return 0;
}

static int BluetoothDeviceLooksLikeHci(UINT8 DeviceClass) {
    UINT16 Total;

    if (DeviceClass == USB_CLASS_WIRELESS) {
        return 1;
    }
    if (GetDesc(USB_WVALUE_DT_CONFIG, 0, 9, gBluetoothConfigBuffer) < 0) {
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
    if (GetDesc(USB_WVALUE_DT_CONFIG, 0, Total, gBluetoothConfigBuffer) < 0) {
        return 0;
    }
    return BluetoothConfigHasHci(gBluetoothConfigBuffer, Total);
}

static int BluetoothFinishProbe(UINT32 RootPort) {
    UINT8 DeviceClass;
    UINT16 VendorId;
    UINT16 ProductId;

    if (gBluetoothSlot == 0) {
        return 0;
    }
    gXferSlot = gBluetoothSlot;
    if (GetDeviceDesc() < 0) {
        BluetoothReleaseSlot();
        return 0;
    }
    DeviceClass = gCtrlBuf[4];
    VendorId = (UINT16)(gCtrlBuf[8] | (gCtrlBuf[9] << 8));
    ProductId = (UINT16)(gCtrlBuf[10] | (gCtrlBuf[11] << 8));
    if (!BluetoothDeviceLooksLikeHci(DeviceClass)) {
        BluetoothReleaseSlot();
        return 0;
    }
    gBluetoothUsbSeen = 1;
    gBluetoothVendorId = VendorId;
    gBluetoothProductId = ProductId;
    gBluetoothRootPort = RootPort;
    {
        char Line[80];
        char Hex[12];
        int N = 0;
        int I;

        const char *P = "Boot: Bluetooth HCI vid=";
        while (*P && N < 70) {
            Line[N++] = *P++;
        }
        HalSerialFormatHex(Hex, VendorId, 4);
        for (I = 0; Hex[I] && N < 70; I++) {
            Line[N++] = Hex[I];
        }
        P = " pid=";
        while (*P && N < 70) {
            Line[N++] = *P++;
        }
        HalSerialFormatHex(Hex, ProductId, 4);
        for (I = 0; Hex[I] && N < 70; I++) {
            Line[N++] = Hex[I];
        }
        P = " class=";
        while (*P && N < 70) {
            Line[N++] = *P++;
        }
        HalSerialFormatHex(Hex, DeviceClass, 2);
        for (I = 0; Hex[I] && N < 70; I++) {
            Line[N++] = Hex[I];
        }
        P = " port=";
        while (*P && N < 70) {
            Line[N++] = *P++;
        }
        HalSerialFormatHex(Hex, RootPort, 2);
        for (I = 0; Hex[I] && N < 70; I++) {
            Line[N++] = Hex[I];
        }
        if (N < 79) {
            Line[N++] = '\n';
        }
        Line[N] = 0;
        BootLog(Line);
    }
    /* btx-1：见后立刻放 slot；不 SetConfig / 不配端点 */
    BluetoothReleaseSlot();
    return 1;
}

/* 仅软 Address：已 CCS+PED；禁止 ForcePR */
static int BluetoothAddressRootSoft(UINT32 Port, UINT8 *SpeedOut) {
    UINT32 PortStatus;

    PortStatus = ReadMmio32(gOperationalBase + PortReg(Port));
    if (!(PortStatus & PORTSC_CCS) || !(PortStatus & PORTSC_PED)) {
        return 0;
    }
    *SpeedOut = PortSpeed(PortStatus);
    BluetoothReleaseSlot();
    if (!AddressDeviceOnPort(Port, *SpeedOut, &gBluetoothSlot,
                             gBluetoothDeviceContext, 0, 0, 0, 0, 0)) {
        BluetoothReleaseSlot();
        return 0;
    }
    if (gBluetoothSlot <= DCBAA_SLOTS) {
        gSlotEp0UsesKbdRing[gBluetoothSlot] = 0;
    }
    return 1;
}

static int BluetoothPortOwned(UINT32 Port) {
    if (gSlotId != 0 && Port == gPort1) {
        return 1;
    }
    if (gMouseSlotId != 0 && Port == gMousePort) {
        return 1;
    }
    if (gHubSlotId != 0 && Port == gHubRootPort) {
        return 1;
    }
    if (gMscPort != 0 && Port == gMscPort) {
        return 1;
    }
    if (gMscBootWatchPort != 0 && Port == gMscBootWatchPort) {
        return 1;
    }
    if (gFtdiClaimed && Port == gFtdiPort) {
        return 1;
    }
    if (gCdcClaimed && Port == gCdcPort) {
        return 1;
    }
    return 0;
}

int XhciBluetoothProbe(void) {
    UINT32 Port;
    UINT8 Speed;

    if (gBluetoothProbeDone) {
        return gBluetoothUsbSeen ? 1 : 0;
    }
    if (!gXhciStarted || gOperationalBase == 0 || gMaxPorts == 0) {
        return -1;
    }
    if (gXhciPortEnumFreeze) {
        BootLog("Boot: Bluetooth probe skip (port enum freeze, keep mouse)\n");
        gBluetoothProbeDone = 1;
        return 0;
    }

    gBluetoothProbeDone = 1;
    for (Port = 1; Port <= gMaxPorts && Port <= 32u; Port++) {
        if (BluetoothPortOwned(Port)) {
            continue;
        }
        if (!BluetoothAddressRootSoft(Port, &Speed)) {
            continue;
        }
        (void)Speed;
        if (BluetoothFinishProbe(Port)) {
            BluetoothRequeueHid();
            return 1;
        }
    }
    /* 不扫 hub 子口；板载蓝牙在根口 */
    BootLog("Boot: Bluetooth HCI not found\n");
    BluetoothRequeueHid();
    return 0;
}
