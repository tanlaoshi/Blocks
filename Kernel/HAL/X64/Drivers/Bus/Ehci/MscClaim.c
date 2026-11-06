/*
 * MscClaim.c — Parse Bulk + FinishClaim / hub 子口（PR-H-ehci-3）
 */
#include "Ehci/Private.h"
#include "SerialLog.h"
#include "Hal.h"

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

static int ParseMscBulk(UINT8 *Cfg, UINT16 Total, UINT8 *OutInterface, UINT8 *EndpointIn,
                        UINT16 *MaxPacketSizeIn, UINT8 *EndpointOut, UINT16 *MaxPacketSizeOut,
                        UINT8 *CfgVal) {
    UINT16 Off = 0;
    UINT8 CurInterface = 0xFF;
    UINT8 CurAlt = 0;
    UINT8 InterfaceClass = 0;
    UINT8 InterfaceSub = 0;
    UINT8 InterfaceProto = 0;
    UINT8 BestInterface = 0xFF;
    UINT8 BestIn = 0;
    UINT8 BestOut = 0;
    UINT16 BestInMaxPacketSize = 0;
    UINT16 BestOutMaxPacketSize = 0;
    int BestScore = -1;

    *CfgVal = 1;
    while (Off + 2 <= Total) {
        UINT8 Len = Cfg[Off];
        UINT8 Type = Cfg[Off + 1];

        if (Len < 2 || Off + Len > Total) {
            break;
        }
        if (Type == 0x02 && Len >= 9) {
            *CfgVal = Cfg[Off + 5];
        } else if (Type == 4 && Len >= 9) {
            CurInterface = Cfg[Off + 2];
            CurAlt = Cfg[Off + 3];
            InterfaceClass = Cfg[Off + 5];
            InterfaceSub = Cfg[Off + 6];
            InterfaceProto = Cfg[Off + 7];
        } else if (Type == 5 && Len >= 7 && CurInterface != 0xFF && CurAlt == 0) {
            UINT8 Addr = Cfg[Off + 2];
            UINT8 Attr = Cfg[Off + 3];
            UINT16 MaxPacketSize = (UINT16)(Cfg[Off + 4] | (Cfg[Off + 5] << 8));
            int Score;

            if ((Attr & 0x03) != 2) {
                Off = (UINT16)(Off + Len);
                continue;
            }
            if (InterfaceClass == 0x08) {
                Score = 10;
                if (InterfaceSub == 0x06) {
                    Score += 2;
                }
                if (InterfaceProto == 0x50 || InterfaceProto == 0x62) {
                    Score += 4;
                }
            } else {
                Score = 0;
            }
            if (Score < BestScore) {
                Off = (UINT16)(Off + Len);
                continue;
            }
            if (Score > BestScore) {
                BestScore = Score;
                BestInterface = CurInterface;
                BestIn = 0;
                BestOut = 0;
                BestInMaxPacketSize = 0;
                BestOutMaxPacketSize = 0;
            } else if (CurInterface != BestInterface) {
                Off = (UINT16)(Off + Len);
                continue;
            }
            if (Addr & 0x80) {
                BestIn = Addr;
                BestInMaxPacketSize = MaxPacketSize;
            } else {
                BestOut = Addr;
                BestOutMaxPacketSize = MaxPacketSize;
            }
        }
        Off = (UINT16)(Off + Len);
    }
    if (BestIn == 0 || BestOut == 0 || BestScore < 10) {
        return 0; /* 须 class8；勿误认 HID/BT */
    }
    *OutInterface = BestInterface;
    *EndpointIn = BestIn;
    *MaxPacketSizeIn = BestInMaxPacketSize;
    *EndpointOut = BestOut;
    *MaxPacketSizeOut = BestOutMaxPacketSize;
    return 1;
}

int MscFinishClaim(EHCI_CTRL *C, UINT8 Speed, UINT8 HubAddr, UINT8 HubPort) {
    USB_DEVICE_DESCRIPTOR Dev;
    UINT8 Cfg[256];
    UINT8 EndpointMaxPacketSize = 64;
    UINT8 Addr;
    UINT8 Interface;
    UINT8 EndpointIn;
    UINT8 EndpointOut;
    UINT16 MaxPacketSizeIn;
    UINT16 MaxPacketSizeOut;
    UINT8 CfgVal;
    UINT16 CfgLen;

    C->TransferSpeed = Speed;
    C->TransferHubAddress = HubAddr;
    C->TransferHubPort = HubPort;

    if (GetDescriptor(C, 0, (Speed == EHCI_SPEED_HS) ? 64 : 8, USB_WVALUE_DT_DEVICE, 8, &Dev) !=
        0) {
        if (GetDescriptor(C, 0, 8, USB_WVALUE_DT_DEVICE, 8, &Dev) != 0) {
            BootMarkUsb("Boot: EHCI MSC getdesc fail\n");
            return 0;
        }
        EndpointMaxPacketSize = 8;
    } else {
        EndpointMaxPacketSize = Dev.bMaxPacketSize0 ? Dev.bMaxPacketSize0 : 8;
    }
    if (GetDescriptor(C, 0, EndpointMaxPacketSize, USB_WVALUE_DT_DEVICE, 18, &Dev) != 0) {
        return 0;
    }
    MscMarkHex4("Boot: EHCI MSC vid=", Dev.idVendor);
    MscMarkHex4(" pid=", Dev.idProduct);
    MscMarkHex4(" cls=", Dev.bDeviceClass);
    BootMarkUsb("\n");

    if (Dev.bDeviceClass == 0x09u) {
        gEhciLastErr = "msc nested hub";
        return 0;
    }
    /* FT232→ehci-4；勿 SetAddress */
    if (Dev.idVendor == 0x0403u &&
        (Dev.idProduct == 0x6001u || Dev.idProduct == 0x6014u ||
         Dev.idProduct == 0x6015u)) {
        BootMarkUsb("Boot: EHCI MSC Skip FTDI\n");
        return 0;
    }
    if (gEhciNextAddr < 2 || gEhciNextAddr > 127) {
        gEhciNextAddr = 2;
    }
    Addr = gEhciNextAddr++;
    if (SetAddress(C, Addr, EndpointMaxPacketSize) != 0) {
        return 0;
    }
    C->TransferSpeed = Speed;
    C->TransferHubAddress = HubAddr;
    C->TransferHubPort = HubPort;
    Delay(100000);
    if (GetDescriptor(C, Addr, EndpointMaxPacketSize, USB_WVALUE_DT_CONFIG, 9, Cfg) != 0) {
        return 0;
    }
    CfgLen = (UINT16)(Cfg[2] | ((UINT16)Cfg[3] << 8));
    if (CfgLen < 9 || CfgLen > sizeof(Cfg)) {
        CfgLen = 64;
    }
    if (GetDescriptor(C, Addr, EndpointMaxPacketSize, USB_WVALUE_DT_CONFIG, CfgLen, Cfg) != 0) {
        return 0;
    }
    if (!ParseMscBulk(Cfg, CfgLen, &Interface, &EndpointIn, &MaxPacketSizeIn, &EndpointOut, &MaxPacketSizeOut,
                      &CfgVal)) {
        gEhciLastErr = "no msc iface";
        BootMarkUsb("Boot: EHCI no msc iface\n");
        return 0;
    }
    if (SetConfig(C, Addr, EndpointMaxPacketSize, CfgVal) != 0) {
        return 0;
    }
    Delay(200000);
    {
        USB_SETUP_PACKET S;
        UINT8 Lun = 0;
        S.bmRequestType = 0xA1;
        S.bRequest = 0xFE;
        S.wValue = 0;
        S.wIndex = Interface;
        S.wLength = 1;
        C->TransferSpeed = Speed;
        C->TransferHubAddress = HubAddr;
        C->TransferHubPort = HubPort;
        (void)EhciControlTransfer(C, Addr, EndpointMaxPacketSize, &S, &Lun);
    }
    C->MscAddr = Addr;
    C->MscSpeed = Speed;
    C->MscHubAddr = HubAddr;
    C->MscHubPort = HubPort;
    C->MscInterface = Interface;
    C->MscEndpointIn = EndpointIn;
    C->MscEndpointOut = EndpointOut;
    if (MaxPacketSizeIn == 0) {
        MaxPacketSizeIn = (Speed == EHCI_SPEED_HS) ? 512 : 64;
    }
    if (MaxPacketSizeOut == 0) {
        MaxPacketSizeOut = (Speed == EHCI_SPEED_HS) ? 512 : 64;
    }
    C->MscMaxPacketSizeIn = MaxPacketSizeIn;
    C->MscMaxPacketSizeOut = MaxPacketSizeOut;
    C->MscDtIn = 0;
    C->MscDtOut = 0;
    C->MscEndpointMax0 = EndpointMaxPacketSize;
    C->MscOk = 1;
    C->MscCapacityOk = 0;
    C->MscBlockCount = 0;
    C->MscBlockSize = 0;
    gEhciMscCtrl = C;
    gEhciLastErr = "msc ok";
    BootMarkUsb("Boot: EHCI MSC claimed\n");
    return 1;
}

int MscEnsureHub(EHCI_CTRL *C, UINT8 Port) {
    USB_DEVICE_DESCRIPTOR Dev;
    UINT8 EndpointMaxPacketSize = 64;
    UINT8 HubAddr;

    if (!PortReset(C, Port)) {
        return 0;
    }
    C->TransferSpeed = EHCI_SPEED_HS;
    C->TransferHubAddress = 0;
    C->TransferHubPort = 0;
    if (GetDescriptor(C, 0, 64, USB_WVALUE_DT_DEVICE, 8, &Dev) != 0) {
        if (GetDescriptor(C, 0, 8, USB_WVALUE_DT_DEVICE, 8, &Dev) != 0) {
            return 0;
        }
        EndpointMaxPacketSize = 8;
    } else {
        EndpointMaxPacketSize = Dev.bMaxPacketSize0 ? Dev.bMaxPacketSize0 : 64;
    }
    if (GetDescriptor(C, 0, EndpointMaxPacketSize, USB_WVALUE_DT_DEVICE, 18, &Dev) != 0) {
        return 0;
    }
    if (Dev.bDeviceClass == 0x09u) {
        if (gEhciNextAddr < 2 || gEhciNextAddr > 127) {
            gEhciNextAddr = 2;
        }
        HubAddr = gEhciNextAddr++;
        if (SetAddress(C, HubAddr, EndpointMaxPacketSize) != 0) {
            return 0;
        }
        C->TransferSpeed = EHCI_SPEED_HS;
        C->TransferHubAddress = 0;
        C->TransferHubPort = 0;
        Delay(100000);
        if (SetConfig(C, HubAddr, EndpointMaxPacketSize, 1) != 0) {
            return 0;
        }
        C->HubAddr = HubAddr;
        BootMarkUsb("Boot: EHCI MSC hub ready\n");
        return MscClaimViaHub(C);
    }
    /* 根口直连 MSC */
    if (!MscFinishClaim(C, EHCI_SPEED_HS, 0, 0)) {
        return 0;
    }
    if (MscCapacity() == 0) {
        return 1;
    }
    BootMarkUsb("Boot: EHCI MSC drop (no capacity)\n");
    (void)MscRelease();
    return 0;
}

