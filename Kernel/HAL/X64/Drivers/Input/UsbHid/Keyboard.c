/*
 * Keyboard.c — 键盘灯。报告入队在 HidInterruptKeyboard.c。
 * 只走主机控制器公共口，不包含 xHCI 私头。
 *
 * HID 1.11 §7.2.2 SET_REPORT Output：wValue=(2<<8)|ReportId=0x0200。
 * 数据须 DMA 可见（对齐缓冲 + FlushDma）；栈上 1 字节曾导致灯不亮。
 */
#include "UsbHostController.h"
#include "UsbHid/Private.h"

#define HID_SET_REPORT_OUTPUT_VALUE 0x0200u

static UINT8 sKeyboardLedOutputByte BLOCKS_ALIGNED(64);

int KeyboardSetLeds(UINT8 Leds) {
    USB_SETUP Setup;
    const USB_HOST_CONTROLLER_OPERATIONS *Operations;

    if (!HidKeyboardReady()) {
        return -1;
    }
    sKeyboardLedOutputByte = Leds;
    FlushDma(&sKeyboardLedOutputByte, sizeof(sKeyboardLedOutputByte));
    Setup.bmRequestType = 0x21;
    Setup.bRequest = 0x09; /* SET_REPORT */
    Setup.wValue = HID_SET_REPORT_OUTPUT_VALUE;
    Setup.wIndex = KeyboardInterface();
    Setup.wLength = 1;
    Operations = HostOperations();
    if (Operations == 0 || Operations->Control == 0) {
        return -1;
    }
    return Operations->Control(0, &Setup, &sKeyboardLedOutputByte, 1);
}
