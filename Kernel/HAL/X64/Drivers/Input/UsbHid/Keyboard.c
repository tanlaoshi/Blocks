/*
 * Keyboard.c — 键盘灯。报告入队在 HidInterruptKeyboard.c。
 * 只走主机控制器公共口，不包含 xHCI 私头。
 */
#include "UsbHostController.h"
#include "UsbHid/Private.h"

int KeyboardSetLeds(UINT8 Leds) {
    USB_SETUP Setup;
    UINT8 LedByte = Leds;

    if (!HidKeyboardReady()) {
        return -1;
    }
    Setup.bmRequestType = 0x21;
    Setup.bRequest = 0x09; /* SET_REPORT */
    Setup.wValue = USB_WVALUE_DT_CONFIG;
    Setup.wIndex = KeyboardInterface();
    Setup.wLength = 1;
    {
        const USB_HOST_CONTROLLER_OPERATIONS *Ops = HostOperations();
        if (Ops == 0 || Ops->Control == 0) {
            return -1;
        }
        return Ops->Control(0, &Setup, &LedByte, 1);
    }
}
