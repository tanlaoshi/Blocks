/*
 * Enumerate.c — PR-F-xhci-1：根口枚举键盘并绑鼠标（编排）
 *
 * 单口试探 / 绑鼠见 EnumeratePort.c。BSS 仍在 Xhci.c。
 */
#include "XHCI/Private.h"

int EnumerateAndBind(void) {
    int RealPc = !HalCpuIsHypervisor();
    UINT32 Port1 = 0;
    int PassMax = 3;

    gPortNoHid = 0;
    gPortNeedForcePr = 0;
    for (int Wait = 0; Wait < PassMax && Port1 == 0; Wait++) {
        if (DiagVerbose()) {
            LogUsb("Boot: XHCI enum pass=");
            {
                char B[12];
                HalSerialFormatHex(B, (UINT64)(UINT32)(Wait + 1), 2);
                LogUsb(B);
                LogUsb("\n");
            }
        }
        for (UINT32 P = 1; P <= gMaxPorts && P <= 32; P++) {
            if (EnumerateTryRootPort(P)) {
                Port1 = gPort1;
                break;
            }
        }
        if (Port1 == 0) {
            if (RealPc) {
                StallMilliseconds(50);
            } else {
                for (volatile int D = 0; D < 40000; D++) {
                }
            }
        }
    }

    if (Port1 == 0) {
        if (RealPc) {
            HalSerialGopMute(0); /* 放弃 xHCI：允许后续 boot 黄字 */
        }
        LogUsb("Boot: XHCI up but no HID keyboard\n");
        BootLog("Boot: XHCI up but no HID keyboard\n");
        if (gEnumerateWhy) {
            BootLog(gEnumerateWhy);
        }
        DebugWrite("XHCI: no keyboard\n");
        for (UINT32 P = 1; P <= gMaxPorts && P <= 32; P++) {
            if (MousePortInitialize(P)) {
                BootLog("Boot: XHCI mouse only\n");
                /*
                 * 键鼠同 hub：键三轮未成（上电慢 / 子口先撞鼠）会只走鼠；
                 * Mouse Via Hub 后须再扫键，否则永无 Via Hub（USB3 hub 真机）。
                 */
                if (gSlotId == 0 && gHubSlotId != 0 &&
                    EnumerateHubChildrenForKeyboard()) {
                    BootLog("Boot: XHCI-HID Keyboard (after mouse-only)\n");
                }
                break;
            }
        }
        gXhciStarted = 1;
        return 1;
    }

    DebugWrite("XHCI: keyboard ready\n");
    /* 与 mouse 同走 BootLog：真机屏上先 keyboard 再 mouse，再由 Probe 打 init returned */
    BootLog("Boot: XHCI-HID Keyboard\n");

    EnumerateBindMouseAfterKeyboard();

    gXhciStarted = 1;
    return 1;
}
