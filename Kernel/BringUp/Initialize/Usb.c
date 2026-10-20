/*
 * Usb.c — UsbInitialize（开机模块表；BringUp 胶水）
 *
 * 探 USB/PS2 输入；无键盘也继续。
 * USB 转串口在挂盘扫口之后认。板载 COM1 在不在都认，两路可以同时用。
 */
#include "KernelModulesPrivate.h"
#include "Hal.h"
#include "DriverInput.h"
#include "SerialLog.h"

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

int UsbInitialize(void) {
    LogBoot("Boot: Input Probe (USB Then PS/2)\n");
    (void)HalUsbInitialize();
    if (DriverInputReady()) {
        LogBoot("Boot: Input Backend Ready\n");
    } else {
        LogBoot("Boot: Input NONE (Continue)\n");
    }
    if (!HalCpuIsHypervisor()) {
        HalInputArmIrq();
        HandoffMouse();
        DrainHid();
    }
    return 0;
}

int UsbUartInitialize(void) {
    static int Done;

    if (Done) {
        return 0;
    }
    Done = 1;
    if (HalUsbUartClaim() != 1) {
        HalSerialBootMarkForce("Boot: USB-UART None\n");
        return 0;
    }
    return 0;
}
