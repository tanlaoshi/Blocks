/*
 * HidMouseClaim.c — PR-S-xhcihid-1：复合设备鼠标认领
 * 从 Hid.c 原样搬家；不改语义。HID 全局仍在 Xhci.c。
 */
#include "UsbHid/Private.h"
#include "UsbHostController.h"

/*
 * 在首次 ConfigEP 前：从当前配置描述符认领复合鼠标 iface（SetInterface/Protocol/Idle）。
 * 成功则写出 EP 参数供 ConfigureInterrupt 一次 Add。
 */
int PrepCompositeMouse(UINT16 Total, UINT8 Speed, UINT8 KeyboardInterface, UINT8 KeyboardEndpoint,
                              UINT8 *EndpointOut, UINT16 *MaxPacketSizeOut, UINT8 *IvOut) {
    UINT8 Interface = 0, EndpointAddress = 0, Interval = 10;
    UINT16 MaxPacketSize = 8;
    UINT8 CurAlt = 0, BestAlt = 0;
    UINT16 Off;

    if (!ParseConfigMouse(gControlBuffer, Total, Speed, &Interface, &EndpointAddress, &MaxPacketSize, &Interval)) {
        return 0;
    }
    if (!HalCpuIsHypervisor() && gMouseParseScore < 2) {
        return 0;
    }
    if (Interface == KeyboardInterface) {
        return 0;
    }
    if ((EndpointAddress & 0x0F) == (KeyboardEndpoint & 0x0F) && ((EndpointAddress ^ KeyboardEndpoint) & 0x80) == 0) {
        return 0;
    }

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

    *EndpointOut = EndpointAddress;
    *MaxPacketSizeOut = MaxPacketSize;
    *IvOut = Interval;
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
        BootLog(Line);
    }
    return 1;
}

/*
 * v8：G102 等被 skip-as-kbd 时，slot 已 Address 在 gDevCtx/gEndpoint0。
 * 勿 Disable+再 Address（真机常 cc=0x04 / Reset 超时 → m=0）。
 * 直接把当前 slot 认领为独立鼠标。
 */
int ClaimAddressedSlotAsMouse(UINT32 RootPort, UINT8 Speed, UINT16 Total,
                                     UINT8 ConfigVal) {
    UINT8 Interface = 0, EndpointAddress = 0, Interval = 10;
    UINT16 MaxPacketSize = 8;
    UINT32 Slot;

    if (HalCpuIsHypervisor() || gSlotId == 0 || gMouseSlotId != 0) {
        return 0;
    }
    if (!ParseConfigMouse(gControlBuffer, Total, Speed, &Interface, &EndpointAddress, &MaxPacketSize, &Interval)) {
        return 0;
    }
    if (gMouseParseScore < 2) {
        return 0;
    }

    Slot = gSlotId;
    CopyMemory(gMouseDevCtx, gDevCtx, sizeof(gMouseDevCtx));
    FlushDma(gMouseDevCtx, sizeof(gMouseDevCtx));
    DcbaaSet(Slot, PointerToPhysical(gMouseDevCtx));
    DcbaaFlush();

    gMouseSlotId = Slot;
    gMousePort = RootPort;
    gMouseRoute = gKeyboardRoute & 0xFFFFFu;
    gMouseHubSlot = gKeyboardHubSlot;
    gMouseTtPort = gKeyboardTransactionTranslatorPort;
    gMouseInterface = Interface;
    gMouseAbsolute = 0;
    /* gSlotEndpoint0UsesKeyboardRing[Slot] 保持 1：EP0 仍用 Address 时的 gEndpoint0 */

    gSlotId = 0;
    gIntrDci = 0;
    gKeyboardInterface = 0;
    gKeyboardRoute = 0;
    gKeyboardHubSlot = 0;
    gKeyboardTransactionTranslatorPort = 0;

    gTransferSlot = gMouseSlotId;
    if (ConfigVal == 0) {
        ConfigVal = 1;
    }
    if (SetConfig(ConfigVal) < 0) {
        BootLog("Boot: XHCI mouse claim SetConfig fail\n");
        DisableSlot(gMouseSlotId);
        gMouseSlotId = 0;
        return 0;
    }
    if (gMouseInterfaceProto == 1 || gMouseInterfaceProto == 2) {
        (void)SetProtocolBoot(Interface);
    }
    if (gMouseInterfaceProto == 1 || gMouseInterfaceProto == 2 || gMouseInterfaceProto == 0xFF) {
        SetIdle(Interface);
    }
    if (!ConfigureMouseInterrupt(gMouseSlotId, EndpointAddress, MaxPacketSize, Interval, Speed)) {
        BootLog("Boot: XHCI mouse claim ConfigEP fail\n");
        DisableSlot(gMouseSlotId);
        gMouseSlotId = 0;
        return 0;
    }
    ZeroMemory(gMouseBuf, sizeof(gMouseBuf));
    (void)InterruptByClass(3, 0, 0);
    BootLogHexV("Boot: XHCI mouse claim score=", gMouseParseScore, 2);
    BootLog("Boot: XHCI-HID Mouse (Claim After Skip-Kbd)\n");
    return 1;
}
