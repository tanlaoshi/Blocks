/*
 * HidEnumerateConfiguration.c — 配置描述符递增重取（PR-NORM-10）
 */
#include "UsbHostController/Private.h"

/*
 * LS：单包 GET8 稳；满长易在第 2 包挂（曾 short=0x0b/0x3b）。
 * 按 8 递增重取；Parse 已见 boot 键/鼠则早停。成功返回 0，失败 -1。
 */
int HidEnumerateFetchConfiguration(UINT8 Hub, UINT8 Port, int LowSpeed, UINT8 Address,
                                   UINT16 Endpoint0MaxPacketSize,
                                   UINT8 ConfigurationHeader[9], UINT8 Configuration[256],
                                   UINT16 *ConfigurationLengthOut) {
    UsbSetup Setup;
    UINT32 Index;
    UINT16 Want;
    UINT16 Length;
    UINT16 Best = 0;
    int ConfigurationTry;
    int Ret;
    UINT8 MouseInterface;
    UINT8 MouseEndpoint;
    UINT16 MouseMaxPacketSize;
    UINT8 MouseScore;
    UINT8 KeyboardInterface;
    UINT8 KeyboardEndpoint;
    UINT16 KeyboardMaxPacketSize;
    UINT8 KeyboardScore;

    Setup.BmRequestType = 0x80;
    Setup.BRequest = 6;
    Setup.WValue = 0x0200;
    Setup.WIndex = 0;
    Setup.WLength = 8;
    TransferTargetSplit(Hub, Port, LowSpeed, Endpoint0MaxPacketSize);
    Ret = ControlMessage(Address, &Setup, ConfigurationHeader, 8);
    if (Ret < 8) {
        if (!LowSpeed) {
            Setup.WLength = 9;
            Ret = ControlMessage(Address, &Setup, ConfigurationHeader, 9);
        }
        if (Ret < 8) {
            HalSerialBootMark("Boot: DWC2 HID cfg8 fail\n");
            return -1;
        }
    }
    Want = (UINT16)(ConfigurationHeader[2] | ((UINT16)ConfigurationHeader[3] << 8));
    if (Want < 9 || Want > 256) {
        Want = 9;
    }
    for (Index = 0; Index < 8; Index++) {
        Configuration[Index] = ConfigurationHeader[Index];
    }
    Best = 8;
    for (Length = 16; Length <= Want; Length = (UINT16)(Length + 8)) {
        if (Length > 256) {
            Length = 256;
        }
        for (ConfigurationTry = 0; ConfigurationTry < (LowSpeed ? 4 : 1);
             ConfigurationTry++) {
            int Got;
            Setup.WLength = Length;
            TransferTargetSplit(Hub, Port, LowSpeed, Endpoint0MaxPacketSize);
            Got = ControlMessage(Address, &Setup, Configuration, (int)Length);
            if (Got > (int)Best) {
                Best = (UINT16)Got;
            }
            if (Got >= (int)Length || Got >= 25) {
                break;
            }
            if (LowSpeed) {
                (void)HubClearTtBuffer(Hub, Port, Address, 0, 1);
                TransferTargetSplit(Hub, Port, LowSpeed, Endpoint0MaxPacketSize);
                Dwc2DelayMilliseconds(15);
            }
        }
        if (Best >= Want || Best >= 25) {
            if (ParseBootKeyboard(Configuration, Best, &KeyboardInterface,
                                  &KeyboardEndpoint, &KeyboardMaxPacketSize,
                                  &KeyboardScore) ||
                ParseBootMouse(Configuration, Best, &MouseInterface, &MouseEndpoint,
                               &MouseMaxPacketSize, &MouseScore)) {
                break;
            }
        }
        if (Length >= Want) {
            break;
        }
    }
    *ConfigurationLengthOut = Best;
    if (Best < Want) {
        HalSerialBootMark("Boot: DWC2 HID cfg best=");
        Dwc2Hex16(Best);
        HalSerialBootMark("/");
        Dwc2Hex16(Want);
        HalSerialBootMark("\n");
    }
    return 0;
}
