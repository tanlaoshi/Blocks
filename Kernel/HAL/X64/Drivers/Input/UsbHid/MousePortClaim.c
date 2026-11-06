/*
 * MousePortClaim.c — 根口 Address / hub / 类过滤（PR-F-xhci-4）
 */
#include "UsbHid/Private.h"
#include "UsbHostController.h"

/*
 * -1 = 已 Address，继续 SetupHid；
 * ≥0 = 已结束（0 失败 / 1 成功，含 hub 子口）。
 */
int MouseClaimPortInitialize(UINT32 Port1, UINT8 *SpeedOut) {
    UINT32 Ps = ReadMmio32(gOperationalBase + PortRegister(Port1));
    UINT16 Total;
    UINT32 WasSlot;
    int Force;
    UINT8 Speed;
    UINT8 DevClass;

    if (gPortNoHid & (1u << Port1)) {
        BootLogHexV("Boot: XHCI mouse skip port=", Port1, 2);
        return 0;
    }
    BootLogHexV("Boot: XHCI mouse try port=", Port1, 2);
    if (!(Ps & PORTSC_CCS)) {
        return 0;
    }
    /*
     * 仅对「本轮已 Address 再 Disable」的口强制 PR（否则 cc=0x04）。
     * 全口 Force PR + 长超时会空转很久，短按电源失效。
     */
    Force = (gPortNeedForcePr & (1u << Port1)) ? 1 : 0;
    if (!ResetPortEx(Port1, Force)) {
        return 0;
    }
    if (Force && !HalCpuIsHypervisor()) {
        StallMilliseconds(20);
    }
    Speed = PortSpeed(ReadMmio32(gOperationalBase + PortRegister(Port1)));
    *SpeedOut = Speed;
    gMousePort = Port1;
    gMouseRoute = 0;
    gMouseHubSlot = 0;
    gMouseTtPort = 0;
    gMouseAbsolute = 0;
    gMouseInterfaceProto = 0;

    if (!AddressDeviceOnPort(Port1, Speed, &gMouseSlotId, gMouseDevCtx, 0, 0, 0, 0, 0)) {
        gMouseSlotId = 0;
        /* 未强制过且 Address 失败：再 Force PR 试一次（真鼠口常见） */
        if (!Force && (gCmdCode == 4 || gCmdCode == 0x11)) {
            if (!ResetPortEx(Port1, 1)) {
                return 0;
            }
            if (!HalCpuIsHypervisor()) {
                StallMilliseconds(20);
            }
            Speed = PortSpeed(ReadMmio32(gOperationalBase + PortRegister(Port1)));
            *SpeedOut = Speed;
            if (!AddressDeviceOnPort(Port1, Speed, &gMouseSlotId, gMouseDevCtx, 0, 0, 0, 0, 0)) {
                gMouseSlotId = 0;
                return 0;
            }
        } else {
            return 0;
        }
    }

    /*
     * 键盘已绑定时，其它根口上的 hub 不会再走 TryHubOnRootPort。
     * 勿把 hub 当 HID（SetConfig/SetIdle → Stall cc=6）；认领 hub 后扫子口鼠标。
     */
    gTransferSlot = gMouseSlotId;
    if (GetDeviceDesc() < 0) {
        gPortNeedForcePr |= (1u << Port1);
        DisableSlot(gMouseSlotId);
        gMouseSlotId = 0;
        return 0;
    }
    DevClass = gControlBuffer[4];
    /* Mass Storage / Wireless：非鼠标，快跳过，避免 SetupHid 超时拖死启动 */
    if (DevClass == 0x08 || DevClass == 0xE0) {
        BootLogHex("Boot: XHCI mouse skip class=", DevClass, 2);
        gPortNoHid |= (1u << Port1);
        DisableSlot(gMouseSlotId);
        gMouseSlotId = 0;
        return 0;
    }
    if (IsHubDeviceDesc()) {
        WasSlot = gMouseSlotId;
        gMouseSlotId = 0;
        BootLogV("Boot: XHCI mouse-scan hub (class 9)\n");
        if (ClaimHubOnRootPort(Port1, Speed, WasSlot)) {
            return EnumerateHubChildrenForMouse();
        }
        return 0;
    }
    if (GetDescriptor(USB_WVALUE_DT_CONFIG, 0, 9, gControlBuffer) == 0) {
        Total = (UINT16)(gControlBuffer[2] | (gControlBuffer[3] << 8));
        if (Total < 9) {
            Total = 9;
        }
        if (Total > sizeof(gControlBuffer)) {
            Total = (UINT16)sizeof(gControlBuffer);
        }
        /*
         * 完整配置描述符：真机部分设备大包会超时；失败则 RecoverEndpoint0 后仍走
         * SetupHidDevice（其内部会再取描述符）。勿在 EP0 失步时直接放弃。
         */
        if (GetDescriptor(USB_WVALUE_DT_CONFIG, 0, Total, gControlBuffer) < 0) {
            BootLog("Boot: XHCI mouse cfg desc retry\n");
            RecoverEndpoint0(gMouseSlotId);
            gTransferSlot = gMouseSlotId;
        } else if (ConfigHasHubInterface(gControlBuffer, Total)) {
            WasSlot = gMouseSlotId;
            gMouseSlotId = 0;
            BootLog("Boot: XHCI mouse-scan hub (iface 9)\n");
            if (ClaimHubOnRootPort(Port1, Speed, WasSlot)) {
                return EnumerateHubChildrenForMouse();
            }
            return 0;
        }
    }
    return -1;
}

/* 1 = EP 就绪；0 = 失败 */
int MouseFinishEndpointInitialize(UINT32 Port1, UINT8 Speed) {
    UINT16 Total;
    UINT8 EndpointAddress = 0, Interval = 10;
    UINT16 MaxPacketSize = 8;
    UINT8 Interface = 0;

    if (GetDescriptor(USB_WVALUE_DT_CONFIG, 0, 9, gControlBuffer) < 0) {
        DisableSlot(gMouseSlotId);
        gMouseSlotId = 0;
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
        RecoverEndpoint0(gMouseSlotId);
        gTransferSlot = gMouseSlotId;
        if (GetDescriptor(USB_WVALUE_DT_CONFIG, 0, Total, gControlBuffer) < 0) {
            DisableSlot(gMouseSlotId);
            gMouseSlotId = 0;
            return 0;
        }
    }
    if (!ParseConfigMouse(gControlBuffer, Total, Speed, &Interface, &EndpointAddress, &MaxPacketSize, &Interval)) {
        DebugWrite("XHCI: mouse no interrupt EP\n");
        BootLog("Boot: XHCI skip non-mouse HID\n");
        DisableSlot(gMouseSlotId);
        gMouseSlotId = 0;
        return 0;
    }
    if (!HalCpuIsHypervisor() && gMouseParseScore < 2) {
        BootLogHexV("Boot: XHCI skip weak root mouse score=", gMouseParseScore, 2);
        DisableSlot(gMouseSlotId);
        gMouseSlotId = 0;
        return 0;
    }
    gMouseInterface = Interface;
    BootLogHexV("Boot: XHCI mouse root score=", gMouseParseScore, 2);
    if (!ConfigureMouseInterrupt(gMouseSlotId, EndpointAddress, MaxPacketSize, Interval, Speed)) {
        DebugWrite("XHCI: mouse endpoint failed\n");
        DisableSlot(gMouseSlotId);
        gMouseSlotId = 0;
        return 0;
    }
    ZeroMemory(gMouseBuf, sizeof(gMouseBuf));
    {
        UINT8 Addr = gMouseDevCtx[12];
        if (Addr == 0 && gMouseSlotId != 0) {
            Addr = (UINT8)gMouseSlotId;
        }
        UsbDeviceNote(Addr, gControlBuffer, Total);
    }
    (void)InterruptByClass(3, 0, 0);
    DebugWrite("XHCI: mouse ready\n");
    if (gMouseAbsolute) {
        BootLog("Boot: XHCI-HID Mouse (Abs)\n");
    } else {
        BootLog("Boot: XHCI-HID Mouse\n");
    }
    (void)Port1;
    return 1;
}
