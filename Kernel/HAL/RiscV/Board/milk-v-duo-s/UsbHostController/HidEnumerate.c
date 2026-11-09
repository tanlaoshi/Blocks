/*
 * HidEnumerate.c — hub 口上 HID boot 枚举编排（bd-duo-usb；PR-NORM-10）
 */
#include "UsbHostController/Private.h"

int EnumerateHidOnPort(UINT8 Hub, UINT8 Port, UINT16 PortStatus) {
    UINT8 DeviceDescriptor[18];
    UINT8 ConfigurationHeader[9];
    UINT8 Configuration[256];
    UINT16 ConfigurationLength;
    UINT8 MouseInterface;
    UINT8 MouseEndpoint;
    UINT16 MouseMaxPacketSize;
    UINT8 MouseScore;
    UINT8 KeyboardInterface;
    UINT8 KeyboardEndpoint;
    UINT16 KeyboardMaxPacketSize;
    UINT8 KeyboardScore;
    UINT8 Address;
    int LowSpeed;
    int DeviceDescriptorBytes;
    UINT16 Endpoint0MaxPacketSize;
    int WantMouse;
    int WantKeyboard;
    int GotMouse;
    int GotKeyboard;

    /*
     * Pi：HS 口跳过（网口等，非 SPLIT HID）。
     * Duo 根口（Hub==0）可直连 HS boot 键/复合：允许，且不做 SPLIT。
     */
    if ((PortStatus & HUB_STAT_HIGH_SPEED) != 0 && Hub != 0) {
        return 0;
    }
    WantMouse = !gMouseOk;
    WantKeyboard = !gKeyboardOk;
    if (!WantMouse && !WantKeyboard) {
        return 0;
    }
    /*
     * FS/LS 在 HS hub 后：ep0 首包 MPS 必须 8（USB2 §5.5.3 / 常见 HID）。
     * 曾用 FS→64 先 GET 18 → SPLIT getdesc18 fail（屏上已见）。
     */
    LowSpeed = ((PortStatus & HUB_STAT_LOW_SPEED) != 0) ? 1 : 0;
    Endpoint0MaxPacketSize = ((PortStatus & HUB_STAT_HIGH_SPEED) != 0) ? 64u : 8u;
    if (HidEnumerateFetchDeviceAndAddress(Hub, Port, LowSpeed, &Endpoint0MaxPacketSize,
                                          DeviceDescriptor, &DeviceDescriptorBytes,
                                          &Address) < 0) {
        return -1;
    }
    (void)DeviceDescriptorBytes;

    if (HidEnumerateFetchConfiguration(Hub, Port, LowSpeed, Address, Endpoint0MaxPacketSize,
                                       ConfigurationHeader, Configuration,
                                       &ConfigurationLength) < 0) {
        return -1;
    }

    GotMouse = ParseBootMouse(Configuration, ConfigurationLength, &MouseInterface,
                              &MouseEndpoint, &MouseMaxPacketSize, &MouseScore);
    GotKeyboard = ParseBootKeyboard(Configuration, ConfigurationLength, &KeyboardInterface,
                                    &KeyboardEndpoint, &KeyboardMaxPacketSize, &KeyboardScore);
    /* 弱键分 + 像样鼠 = 鼠附加 HID，不当键盘（NUC G102 教训） */
    if (GotKeyboard && KeyboardScore < 3 && GotMouse && MouseScore >= 2) {
        HalSerialBootMark("Boot: DWC2 skip mouse+extraHID as kbd\n");
        GotKeyboard = 0;
    }
    /*
     * LS boot 键（CASUE 2a7a:939f 等）：配置读不全时，USB boot 惯例 iface0 + EP1 IN。
     * 仅在已 SET_ADDR、cfg8 有 bConfigurationValue、且本口只要键时启用。
     */
    if (LowSpeed && WantKeyboard && !GotKeyboard && ConfigurationHeader[0] >= 8) {
        HalSerialBootMark("Boot: DWC2 LS kbd boot-fallback ep1\n");
        GotKeyboard = 1;
        KeyboardInterface = 0;
        KeyboardEndpoint = 0x81;
        KeyboardMaxPacketSize = 8;
        KeyboardScore = 3;
    }
    if (!WantMouse) {
        GotMouse = 0;
    }
    if (!WantKeyboard) {
        GotKeyboard = 0;
    }
    if (!GotMouse && !GotKeyboard) {
        HalSerialBootMark("Boot: DWC2 no boot hid\n");
        return 0;
    }

    return HidEnumerateBindBootInterfaces(
        Hub, Port, LowSpeed, Address, Endpoint0MaxPacketSize, ConfigurationHeader[5],
        GotMouse, MouseInterface, MouseEndpoint, MouseMaxPacketSize, GotKeyboard,
        KeyboardInterface, KeyboardEndpoint, KeyboardMaxPacketSize);
}
