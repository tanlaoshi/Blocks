/*
 * Enumerate.c — 根口 / 设备枚举 boot HID（PR-H-ehci-2 · 2j）
 */
#include "Ehci/Private.h"
#include "SerialLog.h"
#include "Hal.h"
#include "HalSerial.h"

int EnumerateGetDescriptor(EHCI_CTRL *C, UINT8 Addr, UINT8 EndpointMaxPacketSize, UINT16 TypeIndex,
                   UINT16 Len, void *Out) {
    USB_SETUP_PACKET S;

    S.bmRequestType = 0x80;
    S.bRequest = 0x06;
    S.wValue = TypeIndex;
    S.wIndex = 0;
    S.wLength = Len;
    return EhciControlTransfer(C, Addr, EndpointMaxPacketSize, &S, Out);
}

int EnumerateSetAddress(EHCI_CTRL *C, UINT8 NewAddr, UINT8 EndpointMaxPacketSize) {
    USB_SETUP_PACKET S;

    S.bmRequestType = 0x00;
    S.bRequest = 0x05;
    S.wValue = NewAddr;
    S.wIndex = 0;
    S.wLength = 0;
    return EhciControlTransfer(C, 0, EndpointMaxPacketSize, &S, 0);
}

int EnumerateSetConfig(EHCI_CTRL *C, UINT8 Addr, UINT8 EndpointMaxPacketSize, UINT8 Cfg) {
    USB_SETUP_PACKET S;

    S.bmRequestType = 0x00;
    S.bRequest = 0x09;
    S.wValue = Cfg;
    S.wIndex = 0;
    S.wLength = 0;
    return EhciControlTransfer(C, Addr, EndpointMaxPacketSize, &S, 0);
}

int EnumerateSetProtocolBoot(EHCI_CTRL *C, UINT8 Addr, UINT8 EndpointMaxPacketSize, UINT8 Interface) {
    USB_SETUP_PACKET S;

    S.bmRequestType = 0x21;
    S.bRequest = 0x0B;
    S.wValue = 0;
    S.wIndex = Interface;
    S.wLength = 0;
    return EhciControlTransfer(C, Addr, EndpointMaxPacketSize, &S, 0);
}

int EnumerateSetIdle(EHCI_CTRL *C, UINT8 Addr, UINT8 EndpointMaxPacketSize, UINT8 Interface) {
    USB_SETUP_PACKET S;

    S.bmRequestType = 0x21;
    S.bRequest = 0x0A;
    S.wValue = 0;
    S.wIndex = Interface;
    S.wLength = 0;
    return EhciControlTransfer(C, Addr, EndpointMaxPacketSize, &S, 0);
}

void EnumerateMarkHex4(const char *Prefix, UINT32 V) {
    char Hex[12];
    char Dig[5];

    BootMarkUsb(Prefix);
    HalSerialFormatHex(Hex, V, 4);
    Dig[0] = Hex[2];
    Dig[1] = Hex[3];
    Dig[2] = Hex[4];
    Dig[3] = Hex[5];
    Dig[4] = 0;
    BootMarkUsb(Dig);
}

static int EnumerateRootPort(EHCI_CTRL *C, UINT8 Port) {
    USB_DEVICE_DESCRIPTOR Dev;
    UINT8 EndpointMaxPacketSize = 64;

    if (!PortReset(C, Port)) {
        return 0;
    }

    C->TransferSpeed = EHCI_SPEED_HS;
    C->TransferHubAddress = 0;
    C->TransferHubPort = 0;

    if (EnumerateGetDescriptor(C, 0, 64, USB_WVALUE_DT_DEVICE, 8, &Dev) != 0) {
        if (EnumerateGetDescriptor(C, 0, 8, USB_WVALUE_DT_DEVICE, 8, &Dev) != 0) {
            BootMarkUsb("Boot: EHCI getdesc fail\n");
            return 0;
        }
        EndpointMaxPacketSize = 8;
    } else {
        EndpointMaxPacketSize = Dev.bMaxPacketSize0 ? Dev.bMaxPacketSize0 : 64;
    }
    if (EnumerateGetDescriptor(C, 0, EndpointMaxPacketSize, USB_WVALUE_DT_DEVICE, 18, &Dev) != 0) {
        gEhciLastErr = "devdesc";
        return 0;
    }

    {
        char Line[4];
        Line[0] = 'p';
        Line[1] = (char)('0' + (Port % 10));
        Line[2] = 0;
        BootMarkUsb("Boot: EHCI ");
        BootMarkUsb(Line);
        EnumerateMarkHex4(" vid=", Dev.idVendor);
        EnumerateMarkHex4(" pid=", Dev.idProduct);
        EnumerateMarkHex4(" cls=", Dev.bDeviceClass);
        BootMarkUsb("\n");
    }

    if (Dev.bDeviceClass == 0x09u) {
        /* 根口是 Intel RMH 等：配置 hub 后扫下游 */
        UINT8 HubAddr;
        UINT8 HubEndpoint = EndpointMaxPacketSize;

        if (gEhciNextAddr < 2 || gEhciNextAddr > 127) {
            gEhciNextAddr = 2;
        }
        HubAddr = gEhciNextAddr++;
        if (EnumerateSetAddress(C, HubAddr, HubEndpoint) != 0) {
            return 0;
        }
        C->TransferSpeed = EHCI_SPEED_HS;
        C->TransferHubAddress = 0;
        C->TransferHubPort = 0;
        Delay(100000);
        if (EnumerateSetConfig(C, HubAddr, HubEndpoint, 1) != 0) {
            gEhciLastErr = "hub cfg";
            return 0;
        }
        C->HubAddr = HubAddr;
        BootMarkUsb("Boot: EHCI hub walk\n");
        return EnumerateHub(C);
    }

    /* 根口直接 HID（少见） */
    C->TransferSpeed = EHCI_SPEED_HS;
    C->TransferHubAddress = 0;
    C->TransferHubPort = 0;
    /* 设备已在 addr0 读过 desc；再走完整 EnumerateDevice 会重复 getdesc——直接用 EnumerateDevice */
    return EnumerateDevice(C, EHCI_SPEED_HS, 0, 0);
}

int EnumerateHid(EHCI_CTRL *C) {
    UINT8 P;
    int AnyCcs = 0;
    const char *Saved = 0;

    if (!C || !C->Sched) {
        return 0;
    }
    SurveyCcs(C);
    for (P = 1; P <= C->NPorts; P++) {
        if ((C->CcsMask & (1u << (P - 1))) == 0) {
            continue;
        }
        AnyCcs = 1;
        if (EnumerateRootPort(C, P)) {
            return 1;
        }
        /* 保留 tok= / hub 相关错，勿被下一根口盖掉 */
        if (gEhciLastErr && (gEhciLastErr[0] == 't' || gEhciLastErr[0] == 'h' ||
                             gEhciLastErr[0] == 'n')) {
            Saved = gEhciLastErr;
        }
    }
    if (!AnyCcs) {
        gEhciLastErr = "ccs=0";
        for (P = 1; P <= C->NPorts; P++) {
            if (EnumerateRootPort(C, P)) {
                return 1;
            }
        }
    } else if (Saved) {
        gEhciLastErr = Saved;
    }
    return 0;
}
