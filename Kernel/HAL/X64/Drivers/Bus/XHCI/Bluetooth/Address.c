/*
 * Address.c — 根口 Enable + Address + 认 HCI（btx-1）→ Configure/Reset（btx-2）
 *
 * 已 PED：软 Address。CCS 无 PED：ForceUntilPed 一次（规格 Port Reset）。
 * freeze 挡 MSC Hot 反复 Force；此处短暂放下只认 HCI。
 */
#include "XHCI/Bluetooth/Private.h"

/*
 * 热路径鼠优先：只消化完成并按完成再武装（ServiceHid→InterruptByClass）。
 * 禁止无条件 QueueIntr/QueueMouseIntr——双投递 HC 不完成 → 鼠死
 * （ClaimPorts.c usb-hid-keep；与 Linux「完成后再 submit URB」同形）。
 */
void BluetoothRequeueHid(void) {
    ServiceHidCompletions();
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
    if (GetDescriptor(USB_WVALUE_DT_CONFIG, 0, 9, gBluetoothConfigBuffer) < 0) {
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
    if (GetDescriptor(USB_WVALUE_DT_CONFIG, 0, Total, gBluetoothConfigBuffer) < 0) {
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
    gTransferSlot = gBluetoothSlot;
    if (GetDeviceDesc() < 0) {
        BluetoothReleaseSlot();
        return 0;
    }
    DeviceClass = gControlBuffer[4];
    VendorId = (UINT16)(gControlBuffer[8] | (gControlBuffer[9] << 8));
    ProductId = (UINT16)(gControlBuffer[10] | (gControlBuffer[11] << 8));
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

    PortStatus = ReadMmio32(gOperationalBase + PortRegister(Port));
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
     *
     * Intel 8087:0A2B：FC09 失败后软 reboot 常卡成「Control 通、Interrupt 无 CC」。
     * 对表 btusb cmd_timeout → 硬复位：先掉 PORTSC.PP 再上电，再 ForceUntilPed。
     */
    {
        UINT64 PortStatusRegister = gOperationalBase + PortRegister(Port);
        UINT32 Neutral;

        /* 掉电：清 PP，保留 RO/RWS Neutral（勿瞎清 CHANGE） */
        PortStatus = ReadMmio32(PortStatusRegister);
        Neutral = PortscNeutral(PortStatus);
        WriteMmio32(PortStatusRegister, Neutral & ~PORTSC_PP);
        Fence();
        StallMilliseconds(150);
        /* 上电 */
        PortStatus = ReadMmio32(PortStatusRegister);
        Neutral = PortscNeutral(PortStatus);
        WriteMmio32(PortStatusRegister, Neutral | PORTSC_PP);
        Fence();
        StallMilliseconds(100);
        BootLogHex("Boot: Bluetooth pp-cycle portsc=",
                   ReadMmio32(PortStatusRegister), 8);
    }

    FreezeSave = gXhciPortEnumerateFreeze;
    gXhciPortEnumerateFreeze = 0;
    if (!ForceUntilPed(Port, &Force)) {
        gXhciPortEnumerateFreeze = FreezeSave;
        BootLogHex("Boot: Bluetooth enable-fail port=", Port, 2);
        return 0;
    }
    gXhciPortEnumerateFreeze = FreezeSave;

    PortStatus = ReadMmio32(gOperationalBase + PortRegister(Port));
    if (!(PortStatus & PORTSC_CCS) || !(PortStatus & PORTSC_PED)) {
        BootLogHex("Boot: Bluetooth not PED port=", Port, 2);
        return 0;
    }
    *SpeedOut = PortSpeed(PortStatus);
    BootLogHex("Boot: Bluetooth post-ped portsc=", PortStatus, 8);
    BootLogHex(" spd=", *SpeedOut, 2);
    BluetoothReleaseSlot();
    if (!AddressDeviceOnPort(Port, *SpeedOut, &gBluetoothSlot,
                             gBluetoothDeviceContext, 0, 0, 0, 0, 0)) {
        BootLogHex("Boot: Bluetooth addr-fail port=", Port, 2);
        BluetoothReleaseSlot();
        return 0;
    }
    if (gBluetoothSlot <= DCBAA_SLOTS) {
        gSlotEndpoint0UsesKeyboardRing[gBluetoothSlot] = 0;
    }
    if (Force) {
        BootLogHex("Boot: Bluetooth enabled port=", Port, 2);
    }
    return 1;
}
