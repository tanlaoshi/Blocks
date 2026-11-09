/*
 * HidEnumerateDevice.c — 设备描述符 + SET_ADDRESS（PR-NORM-10）
 */
#include "UsbHostController/Private.h"

/*
 * 首包 MPS8 → GET8 → SET_ADDR → 可选补 GET18。
 * 成功：*AddressOut / DeviceDescriptor / *DeviceDescriptorBytes；返回 0。
 * 硬失败返回 -1。
 */
int HidEnumerateFetchDeviceAndAddress(UINT8 Hub, UINT8 Port, int LowSpeed,
                                      UINT16 *Endpoint0MaxPacketSize,
                                      UINT8 DeviceDescriptor[18],
                                      int *DeviceDescriptorBytes,
                                      UINT8 *AddressOut) {
    UsbSetup Setup;
    UINT16 Ep0;
    int Ret;
    UINT8 Address;

    Ep0 = 8;
    TransferTargetSplit(Hub, Port, LowSpeed, Ep0);

    Setup.BmRequestType = 0x80;
    Setup.BRequest = 6;
    Setup.WValue = 0x0100;
    Setup.WIndex = 0;
    Setup.WLength = 8;
    Ret = ControlMessage(0, &Setup, DeviceDescriptor, 8);
    if (Ret < 8) {
        HalSerialBootMark("Boot: DWC2 SPLIT getdesc8 fail port=");
        Dwc2Hex16(Port);
        HalSerialBootMark(LowSpeed ? " LS\n" : " FS\n");
        return -1;
    }
    Ep0 = DeviceDescriptor[7] ? (UINT16)DeviceDescriptor[7] : 8u;
    if (Ep0 != 8 && Ep0 != 16 && Ep0 != 32 && Ep0 != 64) {
        Ep0 = 8;
    }
    gTransferEndpoint0MaxPacketSize = Ep0;
    *Endpoint0MaxPacketSize = Ep0;
    /*
     * USB2 §5.5.3：首 8 字节已含 bMaxPacketSize0 → 可 SET_ADDR。
     * 满 18（vid/pid）LS+SPLIT 常挂（CASUE 2a7a:939f 真机）；
     * 键接口在 **配置描述符**，GET18 失败不得弃口——继续 GET_CFG。
     */
    {
        UINT8 Device8[8];
        UINT32 Index;
        for (Index = 0; Index < 8; Index++) {
            Device8[Index] = DeviceDescriptor[Index];
        }
        Setup.WLength = 18;
        Ret = ControlMessage(0, &Setup, DeviceDescriptor, 18);
        if (Ret < 18) {
            HalSerialBootMark("Boot: DWC2 SPLIT getdesc18@0 fail mps=");
            Dwc2Hex16(Ep0);
            HalSerialBootMark(LowSpeed ? " LS\n" : "\n");
            for (Index = 0; Index < 8; Index++) {
                DeviceDescriptor[Index] = Device8[Index];
            }
            if (!LowSpeed) {
                return -1;
            }
        }
    }

    Address = gNextAddress++;
    if (gNextAddress > 100) {
        gNextAddress = 2;
    }
    Setup.BmRequestType = 0x00;
    Setup.BRequest = 5;
    Setup.WValue = Address;
    Setup.WIndex = 0;
    Setup.WLength = 0;
    if (ControlMessage(0, &Setup, 0, 0) < 0) {
        HalSerialBootMark("Boot: DWC2 HID SET_ADDR fail\n");
        return -1;
    }
    Dwc2DelayMilliseconds(LowSpeed ? 10u : 2u);

    if (Ret < 18) {
        Setup.BmRequestType = 0x80;
        Setup.BRequest = 6;
        Setup.WValue = 0x0100;
        Setup.WIndex = 0;
        Setup.WLength = 18;
        Ret = ControlMessage(Address, &Setup, DeviceDescriptor, 18);
        if (Ret < 18) {
            /* LS：MPS 已够；跳过 vid，直接 GET_CFG（串口曾见 2a7a:939f 仍弃口） */
            HalSerialBootMark("Boot: DWC2 skip desc18 → GET_CFG (LS)\n");
        }
    }

    if (Ret >= 18) {
        HalSerialBootMark("Boot: DWC2 HID vid=");
        Dwc2Hex16((UINT16)(DeviceDescriptor[8] | ((UINT16)DeviceDescriptor[9] << 8)));
        HalSerialBootMark(" pid=");
        Dwc2Hex16((UINT16)(DeviceDescriptor[10] | ((UINT16)DeviceDescriptor[11] << 8)));
        HalSerialBootMark(LowSpeed ? " LS\n" : " FS\n");
    } else {
        HalSerialBootMark("Boot: DWC2 HID (no vid) LS ep0=");
        Dwc2Hex16(Ep0);
        HalSerialBootMark("\n");
    }

    *DeviceDescriptorBytes = Ret;
    *AddressOut = Address;
    return 0;
}
