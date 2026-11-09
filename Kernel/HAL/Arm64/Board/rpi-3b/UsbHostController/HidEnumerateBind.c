/*
 * HidEnumerateBind.c — SET_CONFIGURATION + 绑 boot 鼠/键（PR-NORM-10）
 */
#include "UsbHostController/Private.h"

/*
 * 成功返回 Bound（0/1）；SET_CFG 失败返回 -1。
 */
int HidEnumerateBindBootInterfaces(UINT8 Hub, UINT8 Port, int LowSpeed, UINT8 Address,
                                   UINT16 Endpoint0MaxPacketSize, UINT8 ConfigurationValue,
                                   int GotMouse, UINT8 MouseInterface, UINT8 MouseEndpoint,
                                   UINT16 MouseMaxPacketSize, int GotKeyboard,
                                   UINT8 KeyboardInterface, UINT8 KeyboardEndpoint,
                                   UINT16 KeyboardMaxPacketSize) {
    UsbSetup Setup;
    int Bound = 0;

    (void)KeyboardInterface;
    Setup.BmRequestType = 0x00;
    Setup.BRequest = 9;
    Setup.WValue = ConfigurationValue ? ConfigurationValue : 1;
    Setup.WIndex = 0;
    Setup.WLength = 0;
    TransferTargetSplit(Hub, Port, LowSpeed, Endpoint0MaxPacketSize);
    if (ControlMessage(Address, &Setup, 0, 0) < 0) {
        HalSerialBootMark("Boot: DWC2 HID SET_CFG fail\n");
        return -1;
    }

    if (GotMouse) {
        HidSetBootIdle(Address, MouseInterface);
        gMouseAddress = Address;
        gMouseEndpoint = (UINT8)(MouseEndpoint & 0x0Fu);
        gMouseMaxPacketSize = MouseMaxPacketSize ? MouseMaxPacketSize : 8;
        gMouseToggle = DWC2_HC_PID_DATA0;
        gMouseHub = Hub;
        gMousePort = Port;
        gMouseSplit = gTransferDoSplit;
        gMouseLowSpeed = LowSpeed;
        gMouseOk = 1;
        Bound = 1;
        HalSerialBootMark("Boot: DWC2 mouse ep=");
        Dwc2Hex16(gMouseEndpoint);
        HalSerialBootMark(" ok\n");
    }
    if (GotKeyboard) {
        HidSetBootIdle(Address, KeyboardInterface);
        gKeyboardAddress = Address;
        gKeyboardEndpoint = (UINT8)(KeyboardEndpoint & 0x0Fu);
        gKeyboardMaxPacketSize = KeyboardMaxPacketSize ? KeyboardMaxPacketSize : 8;
        gKeyboardToggle = DWC2_HC_PID_DATA0;
        gKeyboardHub = Hub;
        gKeyboardPort = Port;
        gKeyboardSplit = gTransferDoSplit;
        gKeyboardLowSpeed = LowSpeed;
        gKeyboardQueueHead = gKeyboardQueueTail;
        gKeyboardReportPreviousValid = 0;
        gKeyboardOk = 1;
        Bound = 1;
        HalSerialBootMark("Boot: DWC2 kbd ep=");
        Dwc2Hex16(gKeyboardEndpoint);
        HalSerialBootMark(" ok\n");
    }
    return Bound;
}
