/*
 * HubEnumerateMouse.c — PR-S-xhcihub-1：hub 子口鼠标枚举
 *
 * 从 Hub.c 原样搬家；不改语义。Hub 口帮手去掉 static，声明在 Internal.h。
 */
#include "UsbHid/Private.h"
#include "UsbHostController.h"

/* hub 子口找独立鼠标（根口 composite 弱 HID 被跳过时） */
int EnumerateHubChildrenForMouse(void) {
    UINT8 Port;
    UINT8 MaxP = gHubNumPorts;

    if (gHubSlotId == 0 || gMouseSlotId != 0) {
        return 0;
    }
    /*
     * MSC 已认：禁止再对 hub 子口 Reset/Address（与根口 deferred / Wifi 同纪律）。
     * hub EP0 与盘争用 → 同样可打出 bulk cc=0x04。
     */
    if (gMscClaimed || XhciPortEnumerateIsFrozen()) {
        BootLog("Boot: XHCI hub mouse skip (msc claimed or freeze)\n");
        return 0;
    }
    if (MaxP == 0 || MaxP > 15) {
        MaxP = 8;
    }
    for (Port = 1; Port <= MaxP; Port++) {
        UINT32 St = 0;
        UINT8 Speed;
        volatile int D;

        /* 跳过已认 MSC 子口（同 hub） */
        if (gMscHubSlot != 0 && gMscHubSlot == (UINT8)gHubSlotId &&
            Port == gMscTtPort) {
            continue;
        }
        (void)HubSetPortFeature(Port, HUB_FEAT_PORT_POWER);
        if (!HalCpuIsHypervisor()) {
            StallMilliseconds(100);
        } else {
            for (D = 0; D < 80000; D++) {
            }
        }
        if (HubGetPortStatus(Port, &St) < 0) {
            continue;
        }
        if (!(St & HUB_PORT_CONNECTION)) {
            continue;
        }
        BootLogV("Boot: XHCI hub mouse port\n");
        /* 跳过已占用为键盘的子口（同 route） */
        if ((gKeyboardRoute & 0xF) == (UINT32)Port && gSlotId != 0) {
            continue;
        }
        if (HubSetPortFeature(Port, HUB_FEAT_PORT_RESET) < 0) {
            continue;
        }
        {
            int t;
            for (t = 0; t < (HalCpuIsHypervisor() ? 50000 : 40); t++) {
                if (HubGetPortStatus(Port, &St) < 0) {
                    break;
                }
                if (St & HUB_C_PORT_RESET) {
                    (void)HubClearPortFeature(Port, HUB_FEAT_C_PORT_RESET);
                    break;
                }
                if (!HalCpuIsHypervisor()) {
                    StallMilliseconds(5);
                }
            }
            /* 复位完成后须等 PORT_ENABLE，否则 Address 后中断 IN 永不完成 → m=0 */
            for (t = 0; t < (HalCpuIsHypervisor() ? 20000 : 40); t++) {
                if (HubGetPortStatus(Port, &St) < 0) {
                    break;
                }
                if (St & HUB_C_PORT_CONNECTION) {
                    (void)HubClearPortFeature(Port, HUB_FEAT_C_PORT_CONNECTION);
                }
                if (St & HUB_PORT_ENABLE) {
                    break;
                }
                if (!HalCpuIsHypervisor()) {
                    StallMilliseconds(5);
                }
            }
        }
        if (!(St & HUB_PORT_ENABLE)) {
            BootLogV("Boot: XHCI hub mouse not PED\n");
            continue;
        }
        Speed = HubPortSpeed(St);
        if (!AddressDeviceOnPort(gHubRootPort, Speed, &gMouseSlotId, gMouseDevCtx,
                                 (UINT32)Port, (UINT8)gHubSlotId, Port, 0, 0)) {
            gMouseSlotId = 0;
            continue;
        }
        if (!SetupHidDevice(gMouseSlotId, gMouseDevCtx, Speed, ParseConfigMouse, 1)) {
            DisableSlot(gMouseSlotId);
            gMouseSlotId = 0;
            continue;
        }
        {
            UINT8 EndpointAddress = 0, Interval = 10, Interface = 0;
            UINT16 MaxPacketSize = 8;
            UINT16 Total;
            if (GetDescriptor(USB_WVALUE_DT_CONFIG, 0, 9, gControlBuffer) < 0) {
                DisableSlot(gMouseSlotId);
                gMouseSlotId = 0;
                continue;
            }
            Total = (UINT16)(gControlBuffer[2] | (gControlBuffer[3] << 8));
            if (Total < 9) {
                Total = 9;
            }
            if (Total > sizeof(gControlBuffer)) {
                Total = (UINT16)sizeof(gControlBuffer);
            }
            if (GetDescriptor(USB_WVALUE_DT_CONFIG, 0, Total, gControlBuffer) < 0 ||
                !ParseConfigMouse(gControlBuffer, Total, Speed, &Interface, &EndpointAddress, &MaxPacketSize, &Interval)) {
                DisableSlot(gMouseSlotId);
                gMouseSlotId = 0;
                continue;
            }
            /*
             * 真机：拒绝弱 HID（score<2）。hub 上 U 盘/无线棒旁常有 vendor HID，
             * 误绑 → arms mouse=xx 但 PHOTO m=0；真鼠多在其它根口。
             */
            if (!HalCpuIsHypervisor() && gMouseParseScore < 2) {
                BootLogHexV("Boot: XHCI hub skip weak mouse score=", gMouseParseScore, 2);
                DisableSlot(gMouseSlotId);
                gMouseSlotId = 0;
                continue;
            }
            gMousePort = gHubRootPort;
            gMouseInterface = Interface;
            BootLogHex("Boot: XHCI mouse hub ep=", EndpointAddress, 2);
            BootLogHex("Boot: XHCI mouse hub mps=", MaxPacketSize, 2);
            BootLogHex("Boot: XHCI mouse hub iv=", Interval, 2);
            BootLogHex("Boot: XHCI mouse hub spd=", Speed, 1);
            BootLogHex("Boot: XHCI mouse hub score=", gMouseParseScore, 1);
            BootLogHex("Boot: XHCI mouse hub tt=",
                       ((UINT32)gMouseHubSlot << 8) | gMouseTtPort, 4);
            BootLogHex("Boot: XHCI mouse hub route=", gMouseRoute, 2);
            if (!ConfigureMouseInterrupt(gMouseSlotId, EndpointAddress, MaxPacketSize, Interval, Speed)) {
                DisableSlot(gMouseSlotId);
                gMouseSlotId = 0;
                continue;
            }
            {
                UINT32 *EndpointOut = (UINT32 *)(void *)(gMouseDevCtx + gCtxSize * gMouseIntrDci);
                FlushDma(EndpointOut, gCtxSize);
                BootLogHex("Boot: XHCI mouse epst=", EndpointOut[0] & 7u, 1);
            }
            ZeroMemory(gMouseBuf, sizeof(gMouseBuf));
            (void)InterruptByClass(3, 0, 0);
            BootLog("Boot: XHCI-HID Mouse Via Hub\n");
            return 1;
        }
    }
    return 0;
}

