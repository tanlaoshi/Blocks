/*
 * Msc.c — MSC 门面：Ready / Scan / Claim / Release / Present（PR-H-ehci-3）
 */
#include "Ehci/Private.h"
#include "DriverBoot.h"
#include "Msc/UsbMscHost.h"
#include "ToySerialLog.h"

EHCI_CTRL *gEhciMscCtrl;
int gEhciMscInSense;
UINT32 gEhciMscTag;

int MscReady(void) {
    return (gEhciMscCtrl && gEhciMscCtrl->MscOk && gEhciMscCtrl->MscCapacityOk)
               ? 1
               : 0;
}

UINT32 MscBlockCount(void) {
    return (gEhciMscCtrl && gEhciMscCtrl->MscCapacityOk)
               ? gEhciMscCtrl->MscBlockCount
               : 0;
}

UINT32 MscBlockSize(void) {
    return (gEhciMscCtrl && gEhciMscCtrl->MscCapacityOk)
               ? gEhciMscCtrl->MscBlockSize
               : 0;
}

int MscScan(void) {
    int i;
    int Found = 0;

    if (!gEhciReady) {
        return -1;
    }
    for (i = 0; i < gEhciCount; i++) {
        EHCI_CTRL *C = &gEhci[i];
        UINT8 P;
        if (!C->Up || !C->Sched) {
            continue;
        }
        SurveyCcs(C);
        for (P = 1; P <= C->NPorts; P++) {
            if (C->CcsMask & (1u << (P - 1))) {
                Found++;
            }
        }
        if (C->HubAddr) {
            Found++;
        }
    }
    ToyBootMarkUsb("Boot: EHCI MSC scan n=");
    {
        char Dig[3];
        Dig[0] = (char)('0' + (Found % 10));
        Dig[1] = 0;
        ToyBootMarkUsb(Dig);
        ToyBootMarkUsb("\n");
    }
    return Found;
}

int MscClaim(void) {
    int i;

    if (!gEhciReady) {
        return -1;
    }
    if (gEhciMscCtrl && gEhciMscCtrl->MscOk && gEhciMscCtrl->MscCapacityOk) {
        return 1;
    }

    ToyBootMarkUsb("Boot: EHCI MSC claim begin\n");
    for (i = 0; i < gEhciCount; i++) {
        EHCI_CTRL *C = &gEhci[i];
        UINT8 P;

        if (!C->Up || !C->Sched) {
            continue;
        }
        if (C->HubAddr) {
            if (MscClaimViaHub(C)) {
                return 1;
            }
            continue;
        }
        SurveyCcs(C);
        for (P = 1; P <= C->NPorts; P++) {
            if ((C->CcsMask & (1u << (P - 1))) == 0) {
                continue;
            }
            if (MscEnsureHub(C, P)) {
                return 1;
            }
        }
        for (P = 1; P <= C->NPorts; P++) {
            if (MscEnsureHub(C, P)) {
                return 1;
            }
        }
    }
    ToyBootMarkUsb("Boot: EHCI MSC claim none\n");
    return 0;
}

int MscRelease(void) {
    if (gEhciMscCtrl) {
        gEhciMscCtrl->MscOk = 0;
        gEhciMscCtrl->MscCapacityOk = 0;
        gEhciMscCtrl->MscBlockCount = 0;
        gEhciMscCtrl->MscBlockSize = 0;
        gEhciMscCtrl->MscAddr = 0;
    }
    gEhciMscCtrl = 0;
    ToyBootMarkUsb("Boot: EHCI MSC release\n");
    return 0;
}

int MscPresent(void) {
    EHCI_CTRL *C = gEhciMscCtrl;
    UINT16 St;
    UINT16 Ch;

    if (!C || !C->MscOk) {
        return 0;
    }
    if (C->MscHubAddr && C->MscHubPort) {
        if (!MscHubGetStatus(C, C->MscHubAddr, C->MscHubPort, &St, &Ch)) {
            return 0;
        }
        return (St & EHCI_HUB_STAT_CONNECT) ? 1 : 0;
    }
    if (C->Up) {
        SurveyCcs(C);
        return C->CcsMask ? 1 : 0;
    }
    return 0;
}

void MscHostOffer(void) {
    static const MSC_HOST Host = {
        .Kind = MSC_HOST_EHCI,
        .BringUp = 0,
        .Ready = MscReady,
        .Scan = MscScan,
        .Claim = MscClaim,
        .Capacity = MscCapacity,
        .BlockCount = MscBlockCount,
        .BlockSize = MscBlockSize,
        .ReadSectors = MscReadSectors,
        .WriteSectors = MscWriteSectors,
        .Flush = MscFlush,
        .Release = MscRelease,
        .Present = MscPresent,
        .ControllerReady = Ready,
    };

    UsbMscHostRegister(&Host);
}

DRIVER_BOOT(DRIVER_BOOT_MSC_HOST, MscHostOffer);
