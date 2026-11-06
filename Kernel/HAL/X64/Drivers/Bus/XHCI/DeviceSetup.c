/*
 * DeviceSetup.c — PR-S-xhcidevice-1：Set* / SetupHidDevice
 *
 * 从 Device.c 原样搬家；不改语义。无 static 提升。
 */
#include "XHCI/Private.h"

int SetConfig(UINT8 Config) {
    USB_SETUP_PACKET Setup = {
        .bmRequestType = 0x00,
        .bRequest = 0x09,
        .wValue = Config,
        .wIndex = 0,
        .wLength = 0
    };
    return ControlTransfer(&Setup, 0);
}

/* HID SET_PROTOCOL Boot 协议 */
int SetProtocolBoot(UINT8 Interface) {
    USB_SETUP_PACKET Setup = {
        .bmRequestType = 0x21,
        .bRequest = 0x0B,
        .wValue = 0x0000,
        .wIndex = Interface,
        .wLength = 0
    };
    return ControlTransfer(&Setup, 0);
}

/* HID SET_IDLE 请求 */
int SetIdle(UINT8 Interface) {
    USB_SETUP_PACKET Setup = {
        .bmRequestType = 0x21,
        .bRequest = 0x0A,
        .wValue = 0x0000,
        .wIndex = Interface,
        .wLength = 0
    };
    return ControlTransfer(&Setup, 0);
}

/* HID SET_REPORT：输出报告（键盘 LED 等） */
int SetReportOutput(UINT8 Interface, void *Data, UINT16 Length) {
    USB_SETUP_PACKET Setup = {
        .bmRequestType = 0x21,
        .bRequest = 0x09,
        .wValue = USB_WVALUE_DT_CONFIG,
        .wIndex = Interface,
        .wLength = Length
    };
    return ControlTransfer(&Setup, Data);
}

/* HID SET_INTERFACE：激活指定 Alternate（复合键鼠偶见鼠标在 alt>0） */
int SetInterface(UINT8 Interface, UINT8 Alt) {
    USB_SETUP_PACKET Setup = {
        .bmRequestType = 0x01,
        .bRequest = 0x0B,
        .wValue = Alt,
        .wIndex = Interface,
        .wLength = 0
    };
    return ControlTransfer(&Setup, 0);
}

/* HID：取配置描述符、SetConfig、可选 Boot Protocol / SET_IDLE */
int SetupHidDevice(UINT32 SlotId, UINT8 *DevCtx, UINT8 Speed,
                   int (*ParseFn)(UINT8 *, UINT16, UINT8, UINT8 *, UINT8 *,
                                  UINT16 *, UINT8 *),
                   int UseBootProto) {
    gTransferSlot = SlotId;
    (void)DevCtx;

    if (!HalCpuIsHypervisor()) {
        StallMilliseconds(10);
    } else {
        for (volatile int d = 0; d < 500000; d++) {
        }
    }

    /* Address 已建好本 slot 的 EP0 环；勿 RingInitialize/SetTrDeq 打断 Running EP0 */

    if (GetDeviceDesc() < 0) {
        return 0;
    }
    if (GetDescriptor(USB_WVALUE_DT_CONFIG, 0, 9, gControlBuffer) < 0) {
        return 0;
    }
    UINT16 Total = (UINT16)(gControlBuffer[2] | (gControlBuffer[3] << 8));
    if (Total < 9) {
        Total = 9;
    }
    if (Total > sizeof(gControlBuffer)) {
        Total = (UINT16)sizeof(gControlBuffer);
    }
    if (GetDescriptor(USB_WVALUE_DT_CONFIG, 0, Total, gControlBuffer) < 0) {
        return 0;
    }
    UINT8 ConfigVal = gControlBuffer[5];
    if (ConfigVal == 0) {
        ConfigVal = 1;
    }

    UINT8 Interface = 0, EndpointAddress = 0, Interval = 10;
    UINT16 MaxPacketSize = 8;
    UINT8 InterfaceProto = 0xFF;
    int HaveIntr = ParseFn(gControlBuffer, Total, Speed, &Interface, &EndpointAddress, &MaxPacketSize, &Interval);
    if (SetConfig(ConfigVal) < 0) {
        return 0;
    }
    /*
     * SET_PROTOCOL(Boot) 仅对 Boot 接口（kbd Proto=1 / mouse Proto=2）。
     * QEMU usb-tablet 为 Proto=0：发 SET_PROTOCOL 会 Stall(cc=6)，EP0 随后
     * GetDescriptor 全失败 → 鼠标 DisableSlot，日志只有 keyboard 没有 mouse。
     */
    if (UseBootProto) {
        UINT16 Off = 0;
        while (Off + 9 <= Total) {
            UINT8 Len = gControlBuffer[Off];
            UINT8 Type = gControlBuffer[Off + 1];
            if (Len < 2 || Off + Len > Total) {
                break;
            }
            if (Type == 4 && Len >= 9 && gControlBuffer[Off + 2] == Interface) {
                InterfaceProto = gControlBuffer[Off + 7];
                break;
            }
            Off = (UINT16)(Off + Len);
        }
        if (InterfaceProto == 1 || InterfaceProto == 2) {
            SetProtocolBoot(Interface);
        }
    }
    /* SET_IDLE(0)：部分 boot 鼠无此则中断 IN 不吐报告；与共享 EP0 环问题正交 */
    if (InterfaceProto == 1 || InterfaceProto == 2 || InterfaceProto == 0xFF) {
        SetIdle(Interface);
    }
    return HaveIntr;
}

