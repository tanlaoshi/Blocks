/*
 * UsbInit.c — InitializeUsb（开机模块表；Hal 本层）
 *
 * 探 USB/PS2 输入；无键盘也继续。USB-UART 仍在 FS 后 claim。
 */
#include "KernelModulesPrivate.h"
#include "Hal.h"
#include "DriverInput.h"
#include "ToySerialLog.h"

static void HandoffMouse(void) {
    UINT32 CenterX = 512;
    UINT32 CenterY = 384;
    UINT32 ScreenW = 0;
    UINT32 ScreenH = 0;

    HalVideoGetSize(&ScreenW, &ScreenH);
    if (ScreenW > 0) {
        CenterX = ScreenW / 2;
    }
    if (ScreenH > 0) {
        CenterY = ScreenH / 2;
    }
    HalInputMouseHandoffDesktop(CenterX, CenterY);
}

static void DrainHid(void) {
    int Index;
    HAL_KEYBOARD_REPORT Keys;
    HAL_MOUSE_REPORT Mouse;

    for (Index = 0; Index < 8; Index++) {
        HalInputPoll();
    }
    while (HalKeyboardDequeue(&Keys)) {
    }
    while (HalMouseDequeue(&Mouse)) {
    }
}

int InitializeUsb(void) {
    ToyLogBoot("Boot: Input Probe (USB Then PS/2)\n");
    (void)HalUsbInitialize();
    if (ToyDriverInputReady()) {
        ToyLogBoot("Boot: Input Backend Ready\n");
    } else {
        ToyLogBoot("Boot: Input NONE (Continue)\n");
    }
    if (!HalCpuIsHypervisor()) {
        HalInputArmIrq();
        HandoffMouse();
        DrainHid();
    }
    return 0;
}
