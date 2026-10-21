/*
 * Usb.c — UsbInitialize（开机模块表；BringUp 胶水）
 *
 * 探 USB/PS2 输入；无键盘也继续。
 * boot-usbuart-demote：USB-UART 不自动 Claim（串口 App / UsbUartInitialize 按需）。
 * 板载 COM1：EARLY，调试日志。
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

/* 串口 App 按需调用；开机/Worker 勿自动调（boot-usbuart-demote） */
int UsbUartInitialize(void) {
    if (HalUsbUartClaim() != 1) {
        return 0;
    }
    return 0;
}
