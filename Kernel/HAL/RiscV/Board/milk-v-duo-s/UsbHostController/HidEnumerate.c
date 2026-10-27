/*
 * HidEnumerate.c — hub 口上 HID boot 枚举（bd-duo-usb）
 */
#include "UsbHostController/Private.h"

int EnumerateHidOnPort(UINT8 Hub, UINT8 Port, UINT16 PortStatus) {
    UsbSetup Setup;
    UINT8 Dev[18];
    UINT8 CfgHdr[9];
    UINT8 Config[256];
    UINT16 CfgLen;
    UINT8 MIface;
    UINT8 MEp;
    UINT16 MMps;
    UINT8 MScore;
    UINT8 KIface;
    UINT8 KEp;
    UINT16 KMps;
    UINT8 KScore;
    UINT8 Address;
    int Low;
    int Ret;
    UINT16 Ep0;
    int WantMouse;
    int WantKbd;
    int GotMouse;
    int GotKbd;
    int Bound = 0;

    /*
     * Pi：HS 口跳过（网口等，非 SPLIT HID）。
     * Duo 根口（Hub==0）可直连 HS boot 键/复合：允许，且不做 SPLIT。
     */
    if ((PortStatus & HUB_STAT_HIGH_SPEED) != 0 && Hub != 0) {
        return 0;
    }
    WantMouse = !gMouseOk;
    WantKbd = !gKeyboardOk;
    if (!WantMouse && !WantKbd) {
        return 0;
    }
    /*
     * FS/LS 在 HS hub 后：ep0 首包 MPS 必须 8（USB2 §5.5.3 / 常见 HID）。
     * 曾用 FS→64 先 GET 18 → SPLIT getdesc18 fail（屏上已见）。
     */
    Low = ((PortStatus & HUB_STAT_LOW_SPEED) != 0) ? 1 : 0;
    Ep0 = ((PortStatus & HUB_STAT_HIGH_SPEED) != 0) ? 64u : 8u;
    if (Hub == 0) {
        TransferTargetHub();
        gTransferLowSpeed = Low;
        gTransferEndpoint0MaxPacketSize = Ep0;
    } else {
        TransferTargetSplit(Hub, Port, Low, Ep0);
    }

    Setup.BmRequestType = 0x80;
    Setup.BRequest = 6;
    Setup.WValue = 0x0100;
    Setup.WIndex = 0;
    Setup.WLength = 8;
    Ret = ControlMessage(0, &Setup, Dev, 8);
    if (Ret < 8) {
        HalSerialBootMark("Boot: DWC2 SPLIT getdesc8 fail port=");
        Dwc2Hex16(Port);
        HalSerialBootMark(Low ? " LS\n" : " FS\n");
        return -1;
    }
    Ep0 = Dev[7] ? (UINT16)Dev[7] : 8u;
    if (Ep0 != 8 && Ep0 != 16 && Ep0 != 32 && Ep0 != 64) {
        Ep0 = 8;
    }
    gTransferEndpoint0MaxPacketSize = Ep0;
    /*
     * USB2 §5.5.3：首 8 字节已含 bMaxPacketSize0 → 可 SET_ADDR。
     * 满 18（vid/pid）LS+SPLIT 常挂（CASUE 2a7a:939f 真机）；
     * 键接口在 **配置描述符**，GET18 失败不得弃口——继续 GET_CFG。
     */
    {
        UINT8 Dev8[8];
        UINT32 i;
        for (i = 0; i < 8; i++) {
            Dev8[i] = Dev[i];
        }
        Setup.WLength = 18;
        Ret = ControlMessage(0, &Setup, Dev, 18);
        if (Ret < 18) {
            HalSerialBootMark("Boot: DWC2 SPLIT getdesc18@0 fail mps=");
            Dwc2Hex16(Ep0);
            HalSerialBootMark(Low ? " LS\n" : "\n");
            for (i = 0; i < 8; i++) {
                Dev[i] = Dev8[i];
            }
            if (!Low) {
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
    Dwc2DelayMilliseconds(Low ? 10u : 2u);

    if (Ret < 18) {
        Setup.BmRequestType = 0x80;
        Setup.BRequest = 6;
        Setup.WValue = 0x0100;
        Setup.WIndex = 0;
        Setup.WLength = 18;
        Ret = ControlMessage(Address, &Setup, Dev, 18);
        if (Ret < 18) {
            /* LS：MPS 已够；跳过 vid，直接 GET_CFG（串口曾见 2a7a:939f 仍弃口） */
            HalSerialBootMark("Boot: DWC2 skip desc18 → GET_CFG (LS)\n");
        }
    }

    if (Ret >= 18) {
        HalSerialBootMark("Boot: DWC2 HID vid=");
        Dwc2Hex16((UINT16)(Dev[8] | ((UINT16)Dev[9] << 8)));
        HalSerialBootMark(" pid=");
        Dwc2Hex16((UINT16)(Dev[10] | ((UINT16)Dev[11] << 8)));
        HalSerialBootMark(Low ? " LS\n" : " FS\n");
    } else {
        HalSerialBootMark("Boot: DWC2 HID (no vid) LS ep0=");
        Dwc2Hex16(Ep0);
        HalSerialBootMark("\n");
    }

    /*
     * 配置描述符（LS）：单包 GET8 稳；满长易在第 2 包挂（曾 short=0x0b/0x3b）。
     * 按 8 递增重取（每次完整控制传输），保留最长成功缓冲；不够 Parse 再 boot 回退。
     */
    Setup.BmRequestType = 0x80;
    Setup.BRequest = 6;
    Setup.WValue = 0x0200;
    Setup.WIndex = 0;
    {
        UINT32 i;
        UINT16 Want;
        UINT16 L;
        UINT16 Best = 0;
        int CfgTry;

        Setup.WLength = 8;
        TransferTargetSplit(Hub, Port, Low, Ep0);
        Ret = ControlMessage(Address, &Setup, CfgHdr, 8);
        if (Ret < 8) {
            if (!Low) {
                Setup.WLength = 9;
                Ret = ControlMessage(Address, &Setup, CfgHdr, 9);
            }
            if (Ret < 8) {
                HalSerialBootMark("Boot: DWC2 HID cfg8 fail\n");
                return -1;
            }
        }
        Want = (UINT16)(CfgHdr[2] | ((UINT16)CfgHdr[3] << 8));
        if (Want < 9 || Want > sizeof(Config)) {
            Want = 9;
        }
        for (i = 0; i < 8; i++) {
            Config[i] = CfgHdr[i];
        }
        Best = 8;
        for (L = 16; L <= Want; L = (UINT16)(L + 8)) {
            if (L > sizeof(Config)) {
                L = (UINT16)sizeof(Config);
            }
            for (CfgTry = 0; CfgTry < (Low ? 4 : 1); CfgTry++) {
                int Got;
                Setup.WLength = L;
                TransferTargetSplit(Hub, Port, Low, Ep0);
                Got = ControlMessage(Address, &Setup, Config, (int)L);
                if (Got > (int)Best) {
                    Best = (UINT16)Got;
                }
                if (Got >= (int)L || Got >= 25) {
                    break;
                }
                if (Low && Hub != 0) {
                    (void)HubClearTtBuffer(Hub, Port, Address, 0, 1);
                    TransferTargetSplit(Hub, Port, Low, Ep0);
                    Dwc2DelayMilliseconds(15);
                }
            }
            if (Best >= Want || Best >= 25) {
                if (ParseBootKeyboard(Config, Best, &KIface, &KEp, &KMps,
                                      &KScore) ||
                    ParseBootMouse(Config, Best, &MIface, &MEp, &MMps, &MScore)) {
                    break;
                }
            }
            if (L >= Want) {
                break;
            }
        }
        Ret = (int)Best;
        CfgLen = Best;
        if (Best < Want) {
            HalSerialBootMark("Boot: DWC2 HID cfg best=");
            Dwc2Hex16(Best);
            HalSerialBootMark("/");
            Dwc2Hex16(Want);
            HalSerialBootMark("\n");
        }
    }

    GotMouse = ParseBootMouse(Config, CfgLen, &MIface, &MEp, &MMps, &MScore);
    GotKbd = ParseBootKeyboard(Config, CfgLen, &KIface, &KEp, &KMps, &KScore);
    /* 弱键分 + 像样鼠 = 鼠附加 HID，不当键盘（NUC G102 教训） */
    if (GotKbd && KScore < 3 && GotMouse && MScore >= 2) {
        HalSerialBootMark("Boot: DWC2 skip mouse+extraHID as kbd\n");
        GotKbd = 0;
    }
    /*
     * LS boot 键（CASUE 2a7a:939f 等）：配置读不全时，USB boot 惯例 iface0 + EP1 IN。
     * 仅在已 SET_ADDR、cfg8 有 bConfigurationValue、且本口只要键时启用。
     */
    if (Low && WantKbd && !GotKbd && CfgHdr[0] >= 8) {
        HalSerialBootMark("Boot: DWC2 LS kbd boot-fallback ep1\n");
        GotKbd = 1;
        KIface = 0;
        KEp = 0x81;
        KMps = 8;
        KScore = 3;
    }
    if (!WantMouse) {
        GotMouse = 0;
    }
    if (!WantKbd) {
        GotKbd = 0;
    }
    if (!GotMouse && !GotKbd) {
        HalSerialBootMark("Boot: DWC2 no boot hid\n");
        return 0;
    }

    Setup.BmRequestType = 0x00;
    Setup.BRequest = 9;
    Setup.WValue = CfgHdr[5] ? CfgHdr[5] : 1;
    Setup.WIndex = 0;
    Setup.WLength = 0;
    TransferTargetSplit(Hub, Port, Low, Ep0);
    if (ControlMessage(Address, &Setup, 0, 0) < 0) {
        HalSerialBootMark("Boot: DWC2 HID SET_CFG fail\n");
        return -1;
    }

    if (GotMouse) {
        HidSetBootIdle(Address, MIface);
        gMouseAddress = Address;
        gMouseEndpoint = (UINT8)(MEp & 0x0Fu);
        gMouseMaxPacketSize = MMps ? MMps : 8;
        gMouseToggle = DWC2_HC_PID_DATA0;
        gMouseHub = Hub;
        gMousePort = Port;
        gMouseSplit = gTransferDoSplit;
        gMouseLowSpeed = Low;
        gMouseOk = 1;
        Bound = 1;
        HalSerialBootMark("Boot: DWC2 mouse ep=");
        Dwc2Hex16(gMouseEndpoint);
        HalSerialBootMark(" ok\n");
    }
    if (GotKbd) {
        HidSetBootIdle(Address, KIface);
        gKeyboardAddress = Address;
        gKeyboardEndpoint = (UINT8)(KEp & 0x0Fu);
        gKeyboardInterfaceIndex = KIface;
        gKeyboardEndpoint0MaxPacketSize = Ep0 ? Ep0 : (Low ? 8u : 64u);
        gKeyboardMaxPacketSize = KMps ? KMps : 8;
        gKeyboardToggle = DWC2_HC_PID_DATA0;
        gKeyboardHub = Hub;
        gKeyboardPort = Port;
        gKeyboardSplit = gTransferDoSplit;
        gKeyboardLowSpeed = Low;
        gKeyboardOk = 1;
        Bound = 1;
        HalSerialBootMark("Boot: DWC2 kbd ep=");
        Dwc2Hex16(gKeyboardEndpoint);
        HalSerialBootMark(" ok\n");
    }
    return Bound;
}
