/*
 * Root.c — 根口 hub/直连键枚举（bd-duo-usb）
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
    UINT16 RootSt;

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
    HalSerialBootMark("Boot: DWC2 root DESC len=");
    Dwc2Hex16(Desc[0]);
    HalSerialBootMark(" cls=");
    Dwc2Hex16(Cls);
    HalSerialBootMark(" vid=");
    Dwc2Hex16(Vid);
    HalSerialBootMark(" pid=");
    Dwc2Hex16(PacketId);
    HalSerialBootMark("\n");
    /* 全 0：HCDMA/总线地址或 PHY 未就绪 */
    if (Desc[0] == 0 || (Vid == 0 && PacketId == 0)) {
        HalSerialBootMark("Boot: DWC2 DESC bogus (check DMA/PHY)\n");
        return -1;
    }

    /* 根口速度 → 合成 hub PortStatus（EnumerateHidOnPort） */
    RootSt = HUB_STAT_CONNECT | HUB_STAT_ENABLE;
    if (gRootSpeed == DWC2_HPRT0_PRTSPD_LS) {
        RootSt |= HUB_STAT_LOW_SPEED;
    } else if (gRootSpeed == DWC2_HPRT0_PRTSPD_HS) {
        RootSt |= HUB_STAT_HIGH_SPEED;
    }

    /*
     * Duo Type-A：常直连键/复合（bDeviceClass=0/3），非 Pi 板上 hub。
     * Class 9 → hub 扫口；否则根口 EnumerateHid（Hub=0，无 SPLIT）。
     */
    if (Cls != 9) {
        HalSerialBootMark("Boot: DWC2 root HID (no hub)\n");
        /*
         * EnumerateHidOnPort 内 SET_ADDR；此处勿先占 addr1。
         * gNextAddress 从 2 起（Pi）；根设备用 1 起亦可——EnumerateHid 自增。
         */
        if (EnumerateHidOnPort(0, 0, RootSt) > 0) {
            if (gMouseOk || gKeyboardOk) {
                if (DriverInputAttach(&gInputBackend) == 0) {
                    HalSerialBootMark("Boot: DWC2 DriverInputAttach");
                    if (gMouseOk) {
                        HalSerialBootMark(" mouse");
                    }
                    if (gKeyboardOk) {
                        HalSerialBootMark(" kbd");
                    }
                    HalSerialBootMark("\n");
                } else {
                    HalSerialBootMark("Boot: DWC2 DriverInputAttach fail\n");
                }
            }
            return 0;
        }
        HalSerialBootMark("Boot: DWC2 root HID enum fail\n");
        return -1;
    }

    /* SET_ADDRESS（hub） */
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
