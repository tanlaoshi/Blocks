/*
 * EnumDevice.c — 单设备 HID 枚举（PR-H-ehci-2）
 */
#include "Ehci/Private.h"
#include "ToySerialLog.h"
#include "Hal.h"
#include "HalSerial.h"

static int ParseHidIface(UINT8 *Cfg, UINT16 Len, UINT8 WantProto, UINT8 *Iface,
                         UINT8 *Proto, UINT8 *Ep, UINT16 *MaxPkt, UINT8 *Interval,
                         UINT8 *CfgVal) {
    UINT16 Off = 0;
    UINT8 CurIface = 0;
    UINT8 CurProto = 0;
    UINT8 CurSub = 0;
    int InHid = 0;
    int Found = 0;
    UINT8 BestScore = 0;

    *Iface = 0;
    *Proto = 0;
    *Ep = 0;
    *MaxPkt = 8;
    *Interval = 10;
    *CfgVal = 1;

    while (Off + 1 < Len) {
        UINT8 L = Cfg[Off];
        UINT8 T = Cfg[Off + 1];
        if (L < 2 || Off + L > Len) {
            break;
        }
        if (T == 0x02 && L >= 9) {
            *CfgVal = Cfg[Off + 5];
        } else if (T == 0x04 && L >= 9) {
            CurIface = Cfg[Off + 2];
            CurSub = Cfg[Off + 6];
            CurProto = Cfg[Off + 7];
            InHid = (Cfg[Off + 5] == 0x03u);
            if (WantProto == 1 && CurProto != 0 && CurProto != 1) {
                InHid = 0;
            }
            if (WantProto == 2 && CurProto != 0 && CurProto != 2) {
                InHid = 0;
            }
            (void)CurSub;
        } else if (T == 0x05 && L >= 7 && InHid) {
            UINT8 Addr = Cfg[Off + 2];
            UINT8 Attr = Cfg[Off + 3];
            if ((Addr & 0x80u) && ((Attr & 0x03u) == 0x03u)) {
                UINT8 Score = 1;
                if (CurSub == 0x01u) {
                    Score += 2;
                }
                if (WantProto && CurProto == WantProto) {
                    Score += 2;
                }
                if (WantProto == 0 && (CurProto == 1 || CurProto == 2)) {
                    Score += 1;
                }
                if (Score >= BestScore) {
                    BestScore = Score;
                    *Iface = CurIface;
                    *Proto = CurProto ? CurProto : (WantProto ? WantProto : 2);
                    *Ep = (UINT8)(Addr & 0x0Fu);
                    *MaxPkt =
                        (UINT16)(Cfg[Off + 4] | ((UINT16)Cfg[Off + 5] << 8));
                    *Interval = Cfg[Off + 6];
                    Found = 1;
                    if (Score >= 4) {
                        return 1;
                    }
                }
            }
        }
        Off = (UINT16)(Off + L);
    }
    return Found;
}

/* 地址已发给设备，但配置或 HID 失败。复位后设备回到地址 0，号可以再用。 */
static int ReleaseAccepted(EHCI_CTRL *C, UINT8 HubAddr, UINT8 HubPort) {
    C->HidOk = 0;
    if (HubAddr == 0) {
        UINT8 P;
        int Ok = 0;

        for (P = 1; P <= C->NPorts && P < 32; P++) {
            if ((C->CcsMask & (1u << (P - 1))) == 0) {
                continue;
            }
            if (PortReset(C, P)) {
                Ok = 1;
            }
        }
        return Ok;
    }
    {
        UINT8 Speed = 0;

        return MscHubResetPort(C, HubAddr, HubPort, &Speed);
    }
}

static int FailAfterAddr(EHCI_CTRL *C, UINT8 Addr, UINT8 HubAddr, UINT8 HubPort) {
    if (!ReleaseAccepted(C, HubAddr, HubPort)) {
        gEhciNextAddr = (UINT8)(Addr + 1u);
    }
    return 0;
}

/* 在当前 XferSpeed/Hub* 下枚举 addr0 设备为 HID */
int EnumDevice(EHCI_CTRL *C, UINT8 Speed, UINT8 HubAddr, UINT8 HubPort) {
    USB_DEVICE_DESCRIPTOR Dev;
    UINT8 Cfg[256];
    UINT8 EpMax = 64;
    UINT8 Addr;
    UINT8 Iface;
    UINT8 Proto;
    UINT8 Ep;
    UINT16 MaxPkt;
    UINT8 Interval;
    UINT8 CfgVal;
    UINT16 CfgLen;

    C->XferSpeed = Speed;
    C->XferHubAddr = HubAddr;
    C->XferHubPort = HubPort;

    if (EnumGetDesc(C, 0, (Speed == EHCI_SPEED_HS) ? 64 : 8, USB_WVALUE_DT_DEVICE, 8, &Dev) !=
        0) {
        if (EnumGetDesc(C, 0, 8, USB_WVALUE_DT_DEVICE, 8, &Dev) != 0) {
            ToyBootMarkUsb("Boot: EHCI getdesc fail ");
            ToyBootMarkUsb(gEhciLastErr ? gEhciLastErr : "?");
            ToyBootMarkUsb("\n");
            return 0;
        }
        EpMax = 8;
    } else {
        EpMax = Dev.bMaxPacketSize0 ? Dev.bMaxPacketSize0 : 8;
        if (Speed == EHCI_SPEED_HS && EpMax < 64) {
            /* keep */
        }
    }
    if (EnumGetDesc(C, 0, EpMax, USB_WVALUE_DT_DEVICE, 18, &Dev) != 0) {
        gEhciLastErr = "devdesc";
        return 0;
    }

    EnumMarkHex4("Boot: EHCI dev vid=", Dev.idVendor);
    EnumMarkHex4(" pid=", Dev.idProduct);
    EnumMarkHex4(" cls=", Dev.bDeviceClass);
    ToyBootMarkUsb("\n");

    if (Dev.bDeviceClass == 0x09u) {
        gEhciLastErr = "nested hub";
        return 0;
    }

    if (gEhciNextAddr < 2 || gEhciNextAddr > 127) {
        gEhciNextAddr = 2;
    }
    Addr = gEhciNextAddr;
    if (EnumSetAddr(C, Addr, EpMax) != 0) {
        return 0;
    }
    C->XferSpeed = Speed;
    C->XferHubAddr = HubAddr;
    C->XferHubPort = HubPort;
    Delay(100000);

    if (EnumGetDesc(C, Addr, EpMax, USB_WVALUE_DT_CONFIG, 9, Cfg) != 0) {
        return FailAfterAddr(C, Addr, HubAddr, HubPort);
    }
    CfgLen = (UINT16)(Cfg[2] | ((UINT16)Cfg[3] << 8));
    if (CfgLen < 9 || CfgLen > sizeof(Cfg)) {
        CfgLen = 64;
    }
    if (EnumGetDesc(C, Addr, EpMax, USB_WVALUE_DT_CONFIG, CfgLen, Cfg) != 0) {
        return FailAfterAddr(C, Addr, HubAddr, HubPort);
    }
    if (!ParseHidIface(Cfg, CfgLen, 2, &Iface, &Proto, &Ep, &MaxPkt, &Interval,
                       &CfgVal) &&
        !ParseHidIface(Cfg, CfgLen, 1, &Iface, &Proto, &Ep, &MaxPkt, &Interval,
                       &CfgVal) &&
        !ParseHidIface(Cfg, CfgLen, 0, &Iface, &Proto, &Ep, &MaxPkt, &Interval,
                       &CfgVal)) {
        gEhciLastErr = "no hid iface";
        ToyBootMarkUsb("Boot: EHCI no hid iface\n");
        return FailAfterAddr(C, Addr, HubAddr, HubPort);
    }
    if (EnumSetConfig(C, Addr, EpMax, CfgVal) != 0) {
        return FailAfterAddr(C, Addr, HubAddr, HubPort);
    }
    (void)EnumSetProtocolBoot(C, Addr, EpMax, Iface);
    (void)EnumSetIdle(C, Addr, EpMax, Iface);

    gEhciNextAddr = (UINT8)(Addr + 1u);

    C->DevAddr = Addr;
    C->DevSpeed = Speed;
    C->HubAddr = HubAddr;
    C->HubPort = HubPort;
    C->HidIface = Iface;
    C->HidEp = Ep;
    C->HidProto = Proto ? Proto : 2;
    C->HidMaxPkt = MaxPkt ? MaxPkt : 8;
    C->HidInterval = Interval ? Interval : 10;
    C->HidOk = 1;
    gEhciLastErr = "ok";
    ToyBootMarkUsb(C->HidProto == 1 ? "Boot: EHCI HID kbd\n"
                                    : "Boot: EHCI HID mouse\n");
    return 1;
}

