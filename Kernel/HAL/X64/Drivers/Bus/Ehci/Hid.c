/*
 * Hid.c — Bringup + 武装中断 IN（PR-H-ehci-2）
 */
#include "Ehci/Private.h"
#include "SerialLog.h"
#include "Hal.h"
#include "HalSerial.h"

static EHCI_CTRL *gHidCtrl;
int gEhciHidReady;
UINT8 gEhciNextAddr = 2;
UINT8 gEhciIntrDt;

void HidArmIntr(EHCI_CTRL *C) {
    EHCI_QTD *T = &C->Qtds[4];
    UINT32 EndpointChar;
    UINT32 EndpointCap;
    UINT16 MaxPacketSize = C->HidMaxPkt;
    UINT8 Sp = C->DevSpeed;
    UINT32 i;

    if (MaxPacketSize > 64) {
        MaxPacketSize = 8;
    }
    if (Sp > EHCI_SPEED_HS) {
        Sp = EHCI_SPEED_HS;
    }
    EndpointChar = ((UINT32)MaxPacketSize << 16) | ((UINT32)Sp << 12) | ((UINT32)C->HidEndpoint << 8) |
             (UINT32)C->DevAddr;
    /* DTC=1：DT 以 qTD 为准，由 gEhciIntrDt 维护 */
    EndpointChar |= (1u << 14);
    if (Sp != EHCI_SPEED_HS && C->HubAddr) {
        EndpointCap = ((UINT32)C->HubAddr << 16) | ((UINT32)C->HubPort << 23) |
                (0xFCu << 8) | 0x01u;
    } else {
        EndpointCap = 0xFFu;
    }
    C->InterruptQueueHead->EndpointChar = EndpointChar;
    C->InterruptQueueHead->EndpointCap = EndpointCap;
    C->InterruptQueueHead->Current = 0;
    C->InterruptQueueHead->AltNext = EHCI_LINK_TERMINATE;
    C->InterruptQueueHead->Token = 0;
    for (i = 0; i < 5; i++) {
        C->InterruptQueueHead->Buf[i] = 0;
        C->InterruptQueueHead->BufHi[i] = 0;
    }

    for (i = 0; i < 64 && i < 8; i++) {
        C->ReportBuf[i] = 0;
    }
    T->Next = EHCI_LINK_TERMINATE;
    T->AltNext = EHCI_LINK_TERMINATE;
    T->Token = EHCI_QTD_ACTIVE | (3u << 10) | (EHCI_QTD_PID_IN << 8) |
               ((UINT32)MaxPacketSize << 16) | (gEhciIntrDt ? EHCI_QTD_DT : 0);
    T->Buf[0] = (UINT32)PointerPhysical(C->ReportBuf);
    T->Buf[1] = T->Buf[2] = T->Buf[3] = T->Buf[4] = 0;
    T->BufHi[0] = T->BufHi[1] = T->BufHi[2] = T->BufHi[3] = T->BufHi[4] = 0;
    Flush(T, sizeof(*T));
    Flush(C->ReportBuf, 64);
    C->InterruptQueueHead->Next = (UINT32)PointerPhysical(T);
    Flush(C->InterruptQueueHead, sizeof(*C->InterruptQueueHead));
    SchedEnablePeriodic(C);
}

int HidBringup(void) {
    int i;
    int Ok = 0;
    UINT32 OrMask = 0;

    gEhciHidReady = 0;
    gHidCtrl = 0;
    gEhciLastErr = "none";
    gEhciNextAddr = 2;
    gEhciHubNote[0] = 0;
    gEhciIntrDt = 0;
    HidResetQueues();

    /* 插拔/上电晚到：再扫一轮 CCS */
    Delay(300000);
    for (i = 0; i < gEhciCount; i++) {
        if (!gEhci[i].Up) {
            continue;
        }
        SurveyCcs(&gEhci[i]);
        OrMask |= gEhci[i].CcsMask;
        gEhci[i].HidOk = 0;
    }
    gEhciCcsOr = OrMask;
    {
        char Hex[12];
        BootMarkUsb("Boot: EHCI HID CCS=");
        HalSerialFormatHex(Hex, OrMask, 4);
        {
            char Dig[5];
            Dig[0] = Hex[2];
            Dig[1] = Hex[3];
            Dig[2] = Hex[4];
            Dig[3] = Hex[5];
            Dig[4] = 0;
            BootMarkUsb(Dig);
        }
        BootMarkUsb("\n");
    }

    for (i = 0; i < gEhciCount; i++) {
        char Tag[4];
        if (!gEhci[i].Up) {
            continue;
        }
        Tag[0] = '#';
        Tag[1] = (char)('0' + (i % 10));
        Tag[2] = 0;
        BootMarkUsb("Boot: EHCI");
        BootMarkUsb(Tag);
        BootMarkUsb(" enum\n");
        if (!SchedStart(&gEhci[i])) {
            continue;
        }
        if (EnumerateHid(&gEhci[i])) {
            HidArmIntr(&gEhci[i]);
            if (!gHidCtrl) {
                gHidCtrl = &gEhci[i];
            }
            Ok = 1;
            break;
        }
    }
    gEhciHidReady = Ok;
    if (Ok) {
        BootMarkUsb("Boot: EHCI-HID ready (PHOTO 2s)\n");
    } else {
        BootMarkUsb("Boot: EHCI-HID none — ");
        BootMarkUsb(gEhciLastErr ? gEhciLastErr : "?");
        BootMarkUsb("\n");
        if (gEhciHubNote[0]) {
            BootMarkUsb("Boot: EHCI note=");
            BootMarkUsb(gEhciHubNote);
            BootMarkUsb("\n");
        }
        BootMarkUsb("Boot: tip=try other USB3 jack; ehci hid\n");
    }
    Delay(20000000);
    return Ok;
}

int HidReady(void) {
    return gEhciHidReady;
}

