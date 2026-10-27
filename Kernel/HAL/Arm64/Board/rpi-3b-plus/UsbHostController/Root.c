/*
 * Root.c — 根口 hub 枚举（bd-pi-usb）
 */
#include "UsbHostController/Private.h"

int EnumerateRootHub(void) {
    UsbSetup Setup;
    UINT8 Desc[18];
    int Ret;
    UINT16 Vid;
    UINT16 PacketId;
    UINT8 Cls;
    UINT8 Address = 1;

    TransferTargetHub();
    /* GET_DESCRIPTOR(DEVICE) @ addr0 */
    Setup.BmRequestType = 0x80;
    Setup.BRequest = 6;
    Setup.WValue = 0x0100;
    Setup.WIndex = 0;
    Setup.WLength = 18;
    Ret = ControlMessage(0, &Setup, Desc, 18);
    if (Ret < 8) {
        HalSerialBootMark("Boot: DWC2 GET_DESC fail\n");
        return -1;
    }

    Cls = Desc[4];
    Vid = (UINT16)Desc[8] | ((UINT16)Desc[9] << 8);
    PacketId = (UINT16)Desc[10] | ((UINT16)Desc[11] << 8);
    HalSerialBootMark("Boot: DWC2 hub DESC len=");
    Dwc2Hex16(Desc[0]);
    HalSerialBootMark(" cls=");
    Dwc2Hex16(Cls);
    HalSerialBootMark(" vid=");
    Dwc2Hex16(Vid);
    HalSerialBootMark(" pid=");
    Dwc2Hex16(PacketId);
    HalSerialBootMark("\n");
    /* 全 0：多半 HCDMA 未走 0xC0000000 总线别名 */
    if (Desc[0] == 0 || (Vid == 0 && PacketId == 0)) {
        HalSerialBootMark("Boot: DWC2 DESC bogus (check DMA bus addr)\n");
        return -1;
    }

    /* SET_ADDRESS */
    Setup.BmRequestType = 0x00;
    Setup.BRequest = 5;
    Setup.WValue = Address;
    Setup.WIndex = 0;
    Setup.WLength = 0;
    Ret = ControlMessage(0, &Setup, 0, 0);
    if (Ret < 0) {
        HalSerialBootMark("Boot: DWC2 SET_ADDR fail\n");
        return -1;
    }
    Dwc2DelayMilliseconds(2);
    gHubAddress = Address;

    /* GET_DESCRIPTOR again @ new addr（确认控制通路） */
    Setup.BmRequestType = 0x80;
    Setup.BRequest = 6;
    Setup.WValue = 0x0100;
    Setup.WIndex = 0;
    Setup.WLength = 18;
    Ret = ControlMessage(Address, &Setup, Desc, 18);
    if (Ret < 8) {
        HalSerialBootMark("Boot: DWC2 re-GET fail\n");
        return -1;
    }
    HalSerialBootMark("Boot: DWC2 hub addr=");
    Dwc2Hex16(Address);
    HalSerialBootMark(" ok\n");

    HubScanPorts(Address);
    return 0;
}

