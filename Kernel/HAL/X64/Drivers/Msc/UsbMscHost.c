/*
 * UsbMscHost.c — USB MSC 主机表
 */
#include "UsbMscHost.h"

static const MSC_HOST *gHosts[USB_MSC_HOST_MAX];
static UINTN gHostCount;

void UsbMscHostRegister(const MSC_HOST *Host) {
    UINTN i;

    if (!Host || !Host->Ready || !Host->Claim) {
        return;
    }
    for (i = 0; i < gHostCount; i++) {
        if (gHosts[i] == Host || gHosts[i]->Kind == Host->Kind) {
            gHosts[i] = Host;
            return;
        }
    }
    if (gHostCount >= USB_MSC_HOST_MAX) {
        return;
    }
    gHosts[gHostCount++] = Host;
}

UINTN UsbMscHostCount(void) {
    return gHostCount;
}

const MSC_HOST *UsbMscHostGet(UINTN Index) {
    if (Index >= gHostCount) {
        return 0;
    }
    return gHosts[Index];
}

const MSC_HOST *UsbMscHostByKind(int Kind) {
    UINTN i;

    for (i = 0; i < gHostCount; i++) {
        if (gHosts[i]->Kind == Kind) {
            return gHosts[i];
        }
    }
    return 0;
}
