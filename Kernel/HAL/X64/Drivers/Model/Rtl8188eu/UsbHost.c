/*
 * UsbHost.c — USB WiFi 认领表
 */
#include "UsbWifiHost.h"

static const USB_WIFI_HOST *gHosts[USB_WIFI_HOST_MAX];
static UINTN gHostCount;

void UsbWifiHostRegister(const USB_WIFI_HOST *Host) {
    UINTN i;

    if (!Host || !Host->Claim || !Host->Pid) {
        return;
    }
    for (i = 0; i < gHostCount; i++) {
        if (gHosts[i] == Host || gHosts[i]->HostKind == Host->HostKind) {
            gHosts[i] = Host;
            return;
        }
    }
    if (gHostCount >= USB_WIFI_HOST_MAX) {
        return;
    }
    gHosts[gHostCount++] = Host;
}

UINTN UsbWifiHostCount(void) {
    return gHostCount;
}

const USB_WIFI_HOST *UsbWifiHostGet(UINTN Index) {
    if (Index >= gHostCount) {
        return 0;
    }
    return gHosts[Index];
}
