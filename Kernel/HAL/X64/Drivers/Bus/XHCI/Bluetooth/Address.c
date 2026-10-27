/*
 * Address.c — 根口 Enable + Address + 认 HCI（btx-1）→ Configure/Reset（btx-2）
 *
 * 已 PED：软 Address。CCS 无 PED：ForceUntilPed 一次（规格 Port Reset）。
 * freeze 挡 MSC Hot 反复 Force；此处短暂放下只认 HCI。
 */
#include "XHCI/Bluetooth/Private.h"

void BluetoothRequeueHid(void) {
    if (gSlotId != 0 && gIntrDci != 0) {
        QueueIntr();
    }
    if (gMouseSlotId != 0 && gMouseIntrDci != 0) {
        QueueMouseIntr();
    }
}

void BluetoothReleaseSlot(void) {
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

int BluetoothFinishProbe(UINT32 RootPort, UINT8 Speed) {
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
        BootLogHex("Boot: Bluetooth skip class=", DeviceClass, 2);
        BootLogHex(" port=", RootPort, 2);
        BluetoothReleaseSlot();
        return 0;
    }
    gBluetoothUsbSeen = 1;
    gBluetoothVendorId = VendorId;
    gBluetoothProductId = ProductId;
    gBluetoothRootPort = RootPort;
    gBluetoothSpeed = Speed;
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
    /* btx-2：保留 slot → 配端点 → HCI_Reset；失败仍记 Seen，放 slot */
    if (!BluetoothConfigure(RootPort, Speed)) {
        BluetoothReleaseSlot();
        return 1;
    }
    if (!BluetoothHciReset()) {
        BluetoothReleaseSlot();
        gBluetoothConfigured = 0;
        return 1;
    }
    /* btx-3：BD_ADDR + 本地名；失败 Ready=0，slot 仍留（可壳侧重试后刀） */
    if (!BluetoothBringUpIdentity()) {
        BootLog("Boot: Bluetooth identity fail (Ready=0)\n");
    }
    return 1;
}

int BluetoothAddressRoot(UINT32 Port, UINT8 *SpeedOut) {
    UINT32 PortStatus;
    int Force = 0;
    UINT32 FreezeSave;

    PortStatus = ReadMmio32(gOperationalBase + PortReg(Port));
    if (!(PortStatus & PORTSC_CCS)) {
        return 0;
    }
    BootLogHex("Boot: Bluetooth try port=", Port, 2);
    BootLogHex("Boot: Bluetooth portsc=", PortStatus, 8);
    BootLogHex("Boot: Bluetooth ped=", (PortStatus & PORTSC_PED) ? 1u : 0u, 1);

    /*
     * 本机口8：CCS+!PED（0x6E1）→ 纯软探永远 not found。
     * 一次 ForceUntilPed = 规格 Port Reset；禁 HotPoll/NeedForce 循环。
     * freeze 短暂放下：claim 空后 freeze 挡 MSC Force，不挡认 HCI。
     */
    FreezeSave = gXhciPortEnumFreeze;
    gXhciPortEnumFreeze = 0;
    if (!ForceUntilPed(Port, &Force)) {
        gXhciPortEnumFreeze = FreezeSave;
        BootLogHex("Boot: Bluetooth enable-fail port=", Port, 2);
        return 0;
    }
    gXhciPortEnumFreeze = FreezeSave;

    PortStatus = ReadMmio32(gOperationalBase + PortReg(Port));
    if (!(PortStatus & PORTSC_CCS) || !(PortStatus & PORTSC_PED)) {
        BootLogHex("Boot: Bluetooth not PED port=", Port, 2);
        return 0;
    }
    *SpeedOut = PortSpeed(PortStatus);
    BluetoothReleaseSlot();
    if (!AddressDeviceOnPort(Port, *SpeedOut, &gBluetoothSlot,
                             gBluetoothDeviceContext, 0, 0, 0, 0, 0)) {
        BootLogHex("Boot: Bluetooth addr-fail port=", Port, 2);
        BluetoothReleaseSlot();
        return 0;
    }
    if (gBluetoothSlot <= DCBAA_SLOTS) {
        gSlotEp0UsesKbdRing[gBluetoothSlot] = 0;
    }
    if (Force) {
        BootLogHex("Boot: Bluetooth enabled port=", Port, 2);
    }
    return 1;
}
