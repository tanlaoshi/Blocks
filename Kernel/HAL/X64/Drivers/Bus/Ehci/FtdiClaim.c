/*
 * FtdiClaim.c — EHCI 上认 FT232（PR-H-ehci-4）
 *
 * 扫 RMH 子口；VID 0403 / PID 6001·6014·6015；SetConfig + 115200。
 */
#include "Ehci/Private.h"
#include "SerialLog.h"
#include "Hal.h"

#define FTDI_VID         0x0403u
#define FTDI_PID_FT232R  0x6001u
#define FTDI_PID_FT232H  0x6014u
#define FTDI_PID_FT231X  0x6015u
#define FTDI_REQ_RESET      0x00u
#define FTDI_REQ_MODEM_CTRL 0x01u
#define FTDI_REQ_SET_FLOW   0x02u
#define FTDI_REQ_SET_BAUD   0x03u
#define FTDI_REQ_SET_DATA   0x04u
#define FTDI_BAUD_115200    0x001Au
#define FTDI_DTR_RTS_HIGH   0x0303u
#define HUB_FEAT_PORT_POWER 8u
#define HUB_STAT_CONNECT    (1u << 0)

EHCI_CTRL *gEhciFtdiCtrl;
UINT8 gEhciFtdiAddr;
UINT8 gEhciFtdiSpeed;
UINT8 gEhciFtdiHubAddr;
UINT8 gEhciFtdiHubPort;
UINT8 gEhciFtdiEndpointIn;
UINT8 gEhciFtdiEndpointOut;
UINT16 gEhciFtdiMaxPacketSizeIn;
UINT16 gEhciFtdiMaxPacketSizeOut;
UINT8 gEhciFtdiDtIn;
UINT8 gEhciFtdiDtOut;
UINT8 gEhciFtdiEndpointMax0;
int gEhciFtdiOk;

static int IsFtdiPid(UINT16 Pid) {
    return Pid == FTDI_PID_FT232R || Pid == FTDI_PID_FT232H ||
           Pid == FTDI_PID_FT231X;
}

static int GetDescriptor(EHCI_CTRL *C, UINT8 Addr, UINT8 EndpointMaxPacketSize, UINT16 TypeIndex,
                   UINT16 Len, void *Out) {
    USB_SETUP_PACKET S;

    S.bmRequestType = 0x80;
    S.bRequest = 0x06;
    S.wValue = TypeIndex;
    S.wIndex = 0;
    S.wLength = Len;
    return EhciControlTransfer(C, Addr, EndpointMaxPacketSize, &S, Out);
}

static int SetAddress(EHCI_CTRL *C, UINT8 NewAddr, UINT8 EndpointMaxPacketSize) {
    USB_SETUP_PACKET S;

    S.bmRequestType = 0x00;
    S.bRequest = 0x05;
    S.wValue = NewAddr;
    S.wIndex = 0;
    S.wLength = 0;
    return EhciControlTransfer(C, 0, EndpointMaxPacketSize, &S, 0);
}

static int SetConfig(EHCI_CTRL *C, UINT8 Addr, UINT8 EndpointMaxPacketSize, UINT8 Cfg) {
    USB_SETUP_PACKET S;

    S.bmRequestType = 0x00;
    S.bRequest = 0x09;
    S.wValue = Cfg;
    S.wIndex = 0;
    S.wLength = 0;
    return EhciControlTransfer(C, Addr, EndpointMaxPacketSize, &S, 0);
}

static int FtdiVendor(EHCI_CTRL *C, UINT8 Req, UINT16 Value, UINT16 Index) {
    USB_SETUP_PACKET S;

    S.bmRequestType = 0x40;
    S.bRequest = Req;
    S.wValue = Value;
    S.wIndex = Index;
    S.wLength = 0;
    C->TransferSpeed = gEhciFtdiSpeed;
    C->TransferHubAddress = gEhciFtdiHubAddr;
    C->TransferHubPort = gEhciFtdiHubPort;
    return EhciControlTransfer(C, gEhciFtdiAddr, gEhciFtdiEndpointMax0 ? gEhciFtdiEndpointMax0 : 8,
                           &S, 0);
}

static int ParseFtdiBulk(UINT8 *Cfg, UINT16 Total, UINT8 *EndpointIn, UINT16 *MaxPacketSizeIn,
                         UINT8 *EndpointOut, UINT16 *MaxPacketSizeOut, UINT8 *CfgVal) {
    UINT16 Off = 0;
    UINT8 CurAlt = 0;
    UINT8 BestIn = 0;
    UINT8 BestOut = 0;
    UINT16 BestInMaxPacketSize = 0;
    UINT16 BestOutMaxPacketSize = 0;

    *CfgVal = 1;
    while (Off + 2 <= Total) {
        UINT8 Len = Cfg[Off];
        UINT8 Type = Cfg[Off + 1];

        if (Len < 2 || Off + Len > Total) {
            break;
        }
        if (Type == 0x02 && Len >= 9) {
            *CfgVal = Cfg[Off + 5] ? Cfg[Off + 5] : 1;
        } else if (Type == 4 && Len >= 9) {
            CurAlt = Cfg[Off + 3];
        } else if (Type == 5 && Len >= 7 && CurAlt == 0) {
            UINT8 Addr = Cfg[Off + 2];
            UINT8 Attr = Cfg[Off + 3];
            UINT16 MaxPacketSize = (UINT16)(Cfg[Off + 4] | (Cfg[Off + 5] << 8));

            if ((Attr & 0x03) == 2) {
                if (Addr & 0x80) {
                    BestIn = Addr;
                    BestInMaxPacketSize = MaxPacketSize ? MaxPacketSize : 64;
                } else {
                    BestOut = Addr;
                    BestOutMaxPacketSize = MaxPacketSize ? MaxPacketSize : 64;
                }
            }
        }
        Off = (UINT16)(Off + Len);
    }
    if (BestIn == 0 || BestOut == 0) {
        return 0;
    }
    *EndpointIn = BestIn;
    *MaxPacketSizeIn = BestInMaxPacketSize;
    *EndpointOut = BestOut;
    *MaxPacketSizeOut = BestOutMaxPacketSize;
    return 1;
}

static int FinishFtdi(EHCI_CTRL *C, UINT8 Speed, UINT8 HubAddr, UINT8 HubPort) {
    USB_DEVICE_DESCRIPTOR Dev;
    UINT8 Cfg[256];
    UINT8 EndpointMaxPacketSize = 8;
    UINT8 Addr;
    UINT8 EndpointIn;
    UINT8 EndpointOut;
    UINT16 MaxPacketSizeIn;
    UINT16 MaxPacketSizeOut;
    UINT8 CfgVal;
    UINT16 CfgLen;

    C->TransferSpeed = Speed;
    C->TransferHubAddress = HubAddr;
    C->TransferHubPort = HubPort;

    if (GetDescriptor(C, 0, 8, USB_WVALUE_DT_DEVICE, 8, &Dev) != 0) {
        BootMarkUsb("Boot: EHCI FTDI GetDesc8 Fail\n");
        return 0;
    }
    EndpointMaxPacketSize = Dev.bMaxPacketSize0 ? Dev.bMaxPacketSize0 : 8;
    if (GetDescriptor(C, 0, EndpointMaxPacketSize, USB_WVALUE_DT_DEVICE, 18, &Dev) != 0) {
        BootMarkUsb("Boot: EHCI FTDI GetDesc18 Fail\n");
        return 0;
    }
    if (Dev.idVendor != FTDI_VID || !IsFtdiPid(Dev.idProduct)) {
        return 0; /* 非 FTDI：静默换口 */
    }
    BootMarkUsb("Boot: EHCI FTDI Vid=0403 Found\n");
    if (gEhciNextAddr < 2 || gEhciNextAddr > 127) {
        gEhciNextAddr = 2;
    }
    Addr = gEhciNextAddr++;
    if (SetAddress(C, Addr, EndpointMaxPacketSize) != 0) {
        BootMarkUsb("Boot: EHCI FTDI SetAddress Fail\n");
        return 0;
    }
    C->TransferSpeed = Speed;
    C->TransferHubAddress = HubAddr;
    C->TransferHubPort = HubPort;
    Delay(100000);
    if (GetDescriptor(C, Addr, EndpointMaxPacketSize, USB_WVALUE_DT_CONFIG, 9, Cfg) != 0) {
        BootMarkUsb("Boot: EHCI FTDI Cfg9 Fail\n");
        return 0;
    }
    CfgLen = (UINT16)(Cfg[2] | ((UINT16)Cfg[3] << 8));
    if (CfgLen < 9 || CfgLen > sizeof(Cfg)) {
        CfgLen = 64;
    }
    if (GetDescriptor(C, Addr, EndpointMaxPacketSize, USB_WVALUE_DT_CONFIG, CfgLen, Cfg) != 0) {
        BootMarkUsb("Boot: EHCI FTDI Cfg Fail\n");
        return 0;
    }
    if (!ParseFtdiBulk(Cfg, CfgLen, &EndpointIn, &MaxPacketSizeIn, &EndpointOut, &MaxPacketSizeOut, &CfgVal)) {
        BootMarkUsb("Boot: EHCI FTDI No Bulk\n");
        return 0;
    }
    if (SetConfig(C, Addr, EndpointMaxPacketSize, CfgVal) != 0) {
        BootMarkUsb("Boot: EHCI FTDI SetCfg Fail\n");
        return 0;
    }

    gEhciFtdiCtrl = C;
    gEhciFtdiAddr = Addr;
    gEhciFtdiSpeed = Speed;
    gEhciFtdiHubAddr = HubAddr;
    gEhciFtdiHubPort = HubPort;
    gEhciFtdiEndpointIn = EndpointIn;
    gEhciFtdiEndpointOut = EndpointOut;
    gEhciFtdiMaxPacketSizeIn = MaxPacketSizeIn ? MaxPacketSizeIn : 64;
    gEhciFtdiMaxPacketSizeOut = MaxPacketSizeOut ? MaxPacketSizeOut : 64;
    gEhciFtdiDtIn = 0;
    gEhciFtdiDtOut = 0;
    gEhciFtdiEndpointMax0 = EndpointMaxPacketSize;
    gEhciFtdiOk = 0; /* baud 成功后再置 */

    if (FtdiVendor(C, FTDI_REQ_RESET, 0, 0) != 0 ||
        FtdiVendor(C, FTDI_REQ_SET_BAUD, FTDI_BAUD_115200, 0) != 0 ||
        FtdiVendor(C, FTDI_REQ_SET_DATA, 8, 0) != 0) {
        BootMarkUsb("Boot: EHCI FTDI Baud Fail\n");
        gEhciFtdiCtrl = 0;
        return 0;
    }
    (void)FtdiVendor(C, FTDI_REQ_SET_FLOW, 0, 0);
    (void)FtdiVendor(C, FTDI_REQ_MODEM_CTRL, FTDI_DTR_RTS_HIGH, 0);
    gEhciFtdiOk = 1;
    {
        char Probe[] = "ftdi-tx\r\n";
        gEhciFtdiDtOut = 0;
        if (EhciBulkTransfer(C, gEhciFtdiAddr, gEhciFtdiEndpointOut, gEhciFtdiMaxPacketSizeOut,
                         gEhciFtdiSpeed, gEhciFtdiHubAddr, gEhciFtdiHubPort, 0,
                         Probe, 8, &gEhciFtdiDtOut) < 0) {
            BootMarkUsb("Boot: EHCI FTDI Tx Fail ");
            BootMarkUsb(gEhciLastErr ? gEhciLastErr : "?");
            BootMarkUsb("\n");
        } else {
            BootMarkUsb("Boot: EHCI FTDI Tx OK\n");
        }
    }
    LogBoot("Boot: USB-UART FTDI\n");
    BootMarkUsb("Boot: USB-UART FTDI\n");
    return 1;
}

static int TryHubPort(EHCI_CTRL *C, UINT8 HubAddr, UINT8 P) {
    UINT16 St;
    UINT16 Ch;
    UINT8 Sp;

    if (C->HidOk && C->HubAddr == HubAddr && C->HubPort == P) {
        return 0;
    }
    if (C->MscOk && C->MscHubAddr == HubAddr && C->MscHubPort == P) {
        return 0;
    }
    if (!MscHubGetStatus(C, HubAddr, P, &St, &Ch)) {
        return 0;
    }
    if (Ch & HUB_STAT_CONNECT) {
        (void)MscHubClearFeature(C, HubAddr, P, 16u); /* C_PORT_CONNECTION */
    }
    if ((St & HUB_STAT_CONNECT) == 0) {
        return 0;
    }
    if (!MscHubResetPort(C, HubAddr, P, &Sp)) {
        BootMarkUsb("Boot: EHCI FTDI Hub Reset Fail\n");
        return 0;
    }
    BootMarkUsb(Sp == EHCI_SPEED_HS ? "Boot: EHCI FTDI Hub Spd=HS\n"
                    : (Sp == EHCI_SPEED_LS ? "Boot: EHCI FTDI Hub Spd=LS\n"
                                          : "Boot: EHCI FTDI Hub Spd=FS\n"));
    return FinishFtdi(C, Sp, HubAddr, P);
}

int FtdiClaimViaHub(EHCI_CTRL *C) {
    UINT8 HubAddr = C->HubAddr;
    UINT8 Desc[16];
    UINT8 NPorts = 6;
    UINT8 P;

    if (!HubAddr) {
        return 0;
    }
    if (MscHubGetDescriptor(C, HubAddr, Desc, 9)) {
        NPorts = Desc[2] ? Desc[2] : 6;
        if (NPorts > 14) {
            NPorts = 14;
        }
    }
    for (P = 1; P <= NPorts; P++) {
        (void)MscHubSetFeature(C, HubAddr, P, HUB_FEAT_PORT_POWER);
    }
    Delay(500000);
    for (P = 1; P <= NPorts; P++) {
        if (TryHubPort(C, HubAddr, P)) {
            return 1;
        }
    }
    return 0;
}
