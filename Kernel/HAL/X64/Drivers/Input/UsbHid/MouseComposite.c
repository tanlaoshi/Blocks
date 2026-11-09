/*
 * MouseComposite.c — PR-S-xhcimouse-1：复合设备同 slot 绑鼠标
 *
 * 从 Mouse.c 原样搬家；不改语义。无 static 提升；鼠标全局仍在 Xhci.c。
 */
#include "UsbHid/Private.h"
#include "UsbHostController.h"

/*
 * QEMU：USB 键鼠复合设备（同一 slot 上键盘 Proto=1 + 鼠标 Proto=2）。
 * 真机：键已 ConfigEP 后禁止再走本函数（同 slot 加鼠 → k=0；见 EnumerateBindMouseAfterKeyboard）。
 */
int MouseKeyboardSlotInitialize(void) {
    UINT8 EndpointAddress = 0, Interval = 10, Interface = 0;
    UINT16 MaxPacketSize = 8;
    UINT16 Total;
    UINT8 CurAlt = 0;
    UINT8 BestAlt = 0;
    UINT16 Off;

    if (gSlotId == 0 || gMouseSlotId != 0) {
        return 0;
    }
    /* 门禁：真机键 IN 已武装则拒，防漏网调用方再踩坑 */
    if (!HalCpuIsHypervisor() && gIntrDci != 0) {
        BootLog("Boot: XHCI composite-on-kbd refused (kbd armed)\n");
        return 0;
    }

    gTransferSlot = gSlotId;
    if (GetDescriptor(USB_WVALUE_DT_CONFIG, 0, 9, gControlBuffer) < 0) {
        return 0;
    }
    Total = (UINT16)(gControlBuffer[2] | (gControlBuffer[3] << 8));
    if (Total < 9) {
        Total = 9;
    }
    if (Total > sizeof(gControlBuffer)) {
        Total = (UINT16)sizeof(gControlBuffer);
    }
    if (GetDescriptor(USB_WVALUE_DT_CONFIG, 0, Total, gControlBuffer) < 0) {
        return 0;
    }
    if (!ParseConfigMouse(gControlBuffer, Total, gSpeed, &Interface, &EndpointAddress, &MaxPacketSize, &Interval)) {
        return 0;
    }
    /*
     * 真机：拒绝弱评分（多为键盘上的 media/vendor HID，proto=0 且永不报指针）。
     * QEMU tablet 走独立口 MousePortInitialize，不受此限。
     */
    if (!HalCpuIsHypervisor() && gMouseParseScore < 2) {
        BootLogHexV("Boot: XHCI skip weak composite score=", gMouseParseScore, 2);
        return 0;
    }
    if (Interface == gKeyboardInterface) {
        return 0;
    }
    if ((EndpointAddress & 0x0F) == (gKeyboardEndpointAddress & 0x0F) && ((EndpointAddress ^ gKeyboardEndpointAddress) & 0x80) == 0) {
        return 0;
    }

    /* 找回该 iface 的 bAlternateSetting（Parse 未导出） */
    Off = 0;
    while (Off + 9 <= Total) {
        UINT8 Len = gControlBuffer[Off];
        UINT8 Type = gControlBuffer[Off + 1];
        if (Len < 2 || Off + Len > Total) {
            break;
        }
        if (Type == 4 && Len >= 9) {
            CurAlt = gControlBuffer[Off + 3];
            if (gControlBuffer[Off + 2] == Interface && gControlBuffer[Off + 5] == 3 &&
                gControlBuffer[Off + 7] != 1) {
                BestAlt = CurAlt;
            }
        }
        Off = (UINT16)(Off + Len);
    }

    gMousePort = gPort1;
    gMouseInterface = Interface;
    (void)SetInterface(Interface, BestAlt);
    if (gMouseInterfaceProto == 1 || gMouseInterfaceProto == 2) {
        (void)SetProtocolBoot(Interface);
    }
    (void)SetIdle(Interface);

    BootLogHexV("Boot: XHCI mouse iface=", Interface, 2);
    BootLogHexV("Boot: XHCI mouse proto=", gMouseInterfaceProto, 2);
    BootLogHexV("Boot: XHCI mouse ep=", EndpointAddress, 2);
    BootLogHexV("Boot: XHCI mouse mps=", MaxPacketSize, 2);
    /* 单行汇总：串口好抄 */
    {
        char Line[72];
        char Hex[12];
        int n = 0;
        const char *P = "Boot: XHCI mouse cfg i=";
        while (*P && n < 28) {
            Line[n++] = *P++;
        }
        HalSerialFormatHex(Hex, Interface, 2);
        P = Hex;
        while (*P && n < 40) {
            Line[n++] = *P++;
        }
        P = " p=";
        while (*P && n < 44) {
            Line[n++] = *P++;
        }
        HalSerialFormatHex(Hex, gMouseInterfaceProto, 2);
        P = Hex;
        while (*P && n < 48) {
            Line[n++] = *P++;
        }
        P = " ep=";
        while (*P && n < 54) {
            Line[n++] = *P++;
        }
        HalSerialFormatHex(Hex, EndpointAddress, 2);
        P = Hex;
        while (*P && n < 58) {
            Line[n++] = *P++;
        }
        P = " mps=";
        while (*P && n < 64) {
            Line[n++] = *P++;
        }
        HalSerialFormatHex(Hex, MaxPacketSize, 2);
        P = Hex;
        while (*P && n < 68) {
            Line[n++] = *P++;
        }
        Line[n++] = '\n';
        Line[n] = 0;
        BootLog(Line); /* 真机 BootMark → COM1 + 屏 */
    }

    {
        int MouseCfg;

        MouseCfg = ConfigureMouseInterrupt(gSlotId, EndpointAddress, MaxPacketSize, Interval, gSpeed);
        if (!MouseCfg) {
            BootLog("Boot: XHCI composite mouse ep fail\n");
            gMouseIntrDci = 0;
            gMouseEndpointAddress = 0;
            return 0;
        }

        gMouseSlotId = gSlotId;
        ZeroMemory(gReportBuf, 8);
        ZeroMemory(gMouseBuf, sizeof(gMouseBuf));
        /*
         * MouseCfg：1=add-only 键盘未停 → 勿 Sync；2/3=曾 Stop/Drop → Sync 键盘。
         * Arm 同样勿再 Sync（PHOTO 上 Sync 后 k 仍 0）。
         */
        if (MouseCfg >= 2 && gSlotId != 0 && gIntrDci != 0) {
            if (SyncKeyboardIntr() == 0) {
                BootLog("Boot: XHCI sync kbd after mouse\n");
            } else {
                BootLog("Boot: XHCI sync kbd after mouse fail\n");
            }
            QueueIntr();
        }
        (void)InterruptByClass(3, 0, 0);
        BootLog("Boot: XHCI composite kbd rearm\n");
    }
    {
        char Line[80];
        int n = 0;
        const char *P = "Boot: XHCI kbd iface=";
        while (*P && n < 24) {
            Line[n++] = *P++;
        }
        Line[n++] = (char)('0' + ((gKeyboardInterface / 10) % 10));
        Line[n++] = (char)('0' + (gKeyboardInterface % 10));
        P = " dci=";
        while (*P && n < 36) {
            Line[n++] = *P++;
        }
        Line[n++] = (char)('0' + ((gIntrDci / 10) % 10));
        Line[n++] = (char)('0' + (gIntrDci % 10));
        P = " mouse dci=";
        while (*P && n < 56) {
            Line[n++] = *P++;
        }
        Line[n++] = (char)('0' + ((gMouseIntrDci / 10) % 10));
        Line[n++] = (char)('0' + (gMouseIntrDci % 10));
        Line[n++] = '\n';
        Line[n] = 0;
        BootLog(Line);
    }
    if (gMouseAbsolute) {
        BootLog("Boot: XHCI-HID Mouse (Composite Abs)\n");
    } else {
        BootLog("Boot: XHCI-HID Mouse (Composite)\n");
    }
    return 1;
}

