/*
 * EthernetEnumerate.c — hub 口 LAN9514 附着（bd-pi-usb / rnet-2）
 */
#include "UsbHostController/Private.h"

#if !BOARD_IS_VIRT
/*
 * rnet-2：HS 口上 LAN9514 以太网功能（0424:ec00）。无 SPLIT。
 * 端点：配置描述符扫 bulk；缺省 IN=1 OUT=2 MPS=512（U-Boot 常见）。
 */
int EnumerateEthernetOnPort(UINT8 Hub, UINT8 Port) {
    UsbSetup Setup;
    UINT8 Dev[18];
    UINT8 Config[128];
    UINT16 Vid;
    UINT16 PacketId;
    UINT16 Ep0 = 64;
    UINT8 Address;
    UINT8 EndpointIn = 1;
    UINT8 EpOut = 2;
    UINT16 MaxPacketSize = 512;
    int Ret;
    int HaveIn = 0;
    int HaveOut = 0;
    UINT32 i;

    if (gEthernetAttached) {
        return 0;
    }
    TransferTargetHighSpeed(Hub, Port, Ep0);
    Setup.BmRequestType = 0x80;
    Setup.BRequest = 6;
    Setup.WValue = 0x0100;
    Setup.WIndex = 0;
    Setup.WLength = 8;
    Ret = ControlMessage(0, &Setup, Dev, 8);
    if (Ret < 8) {
        HalSerialBootMark("Boot: DWC2 ETH getdesc8 fail\n");
        return -1;
    }
    Ep0 = Dev[7] ? (UINT16)Dev[7] : 64u;
    gTransferEndpoint0MaxPacketSize = Ep0;
    Setup.WLength = 18;
    Ret = ControlMessage(0, &Setup, Dev, 18);
    if (Ret < 18) {
        HalSerialBootMark("Boot: DWC2 ETH getdesc18 fail\n");
        return -1;
    }
    Vid = (UINT16)(Dev[8] | ((UINT16)Dev[9] << 8));
    PacketId = (UINT16)(Dev[10] | ((UINT16)Dev[11] << 8));
    HalSerialBootMark("Boot: DWC2 ETH vid=");
    Dwc2Hex16(Vid);
    HalSerialBootMark(" pid=");
    Dwc2Hex16(PacketId);
    HalSerialBootMark("\n");
    if (Vid != 0x0424u || PacketId != 0xec00u) {
        return 0; /* 非 smsc 网口 */
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
        HalSerialBootMark("Boot: DWC2 ETH SET_ADDR fail\n");
        return -1;
    }
    Dwc2DelayMilliseconds(2);
    TransferTargetHighSpeed(Hub, Port, Ep0);

    Setup.BmRequestType = 0x80;
    Setup.BRequest = 6;
    Setup.WValue = 0x0200;
    Setup.WIndex = 0;
    Setup.WLength = 9;
    Ret = ControlMessage(Address, &Setup, Config, 9);
    if (Ret >= 4) {
        UINT16 Total = (UINT16)(Config[2] | ((UINT16)Config[3] << 8));
        if (Total > sizeof(Config)) {
            Total = (UINT16)sizeof(Config);
        }
        if (Total > 9) {
            Setup.WLength = Total;
            Ret = ControlMessage(Address, &Setup, Config, (int)Total);
        }
        if (Ret >= 9) {
            for (i = 0; i + 1u < (UINT32)Ret; ) {
                UINT8 Length = Config[i];
                UINT8 Type = Config[i + 1u];
                if (Length < 2) {
                    break;
                }
                if (Type == 0x05u && Length >= 7u) { /* endpoint */
                    UINT8 AddrEp = Config[i + 2u];
                    UINT8 Attr = Config[i + 3u];
                    UINT16 Eps = (UINT16)(Config[i + 4u] | ((UINT16)Config[i + 5u] << 8));
                    if ((Attr & 0x03u) == 0x02u) { /* bulk */
                        if (AddrEp & 0x80u) {
                            EndpointIn = AddrEp & 0x7Fu;
                            MaxPacketSize = Eps ? Eps : MaxPacketSize;
                            HaveIn = 1;
                        } else {
                            EpOut = AddrEp & 0x7Fu;
                            if (Eps) {
                                MaxPacketSize = Eps;
                            }
                            HaveOut = 1;
                        }
                    }
                }
                i += Length;
            }
        }
    }
    if (!HaveIn || !HaveOut) {
        HalSerialBootMark("Boot: DWC2 ETH eps default in=1 out=2\n");
    }

    Setup.BmRequestType = 0x00;
    Setup.BRequest = 9;
    Setup.WValue = 1;
    Setup.WIndex = 0;
    Setup.WLength = 0;
    if (ControlMessage(Address, &Setup, 0, 0) < 0) {
        HalSerialBootMark("Boot: DWC2 ETH SET_CFG fail\n");
        return -1;
    }
    Dwc2DelayMilliseconds(10);
    if (BoardEthernetUsbAttach(Address, Hub, Port, EndpointIn, EpOut, MaxPacketSize) != 0) {
        return -1;
    }
    gEthernetAttached = 1;
    return 1;
}
#endif
