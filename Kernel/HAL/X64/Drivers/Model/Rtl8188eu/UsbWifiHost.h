/*
 * UsbWifiHost.h — USB WiFi 棒认领表（Wifi ↔ xHCI/EHCI，互不 include 总头）
 */
#ifndef USB_WIFI_HOST_H
#define USB_WIFI_HOST_H

#include "BootTypes.h"

#define USB_WIFI_HOST_MAX 4

#define WIFI_HOST_NONE  0
#define WIFI_HOST_XHCI  1
#define WIFI_HOST_EHCI  2

typedef struct USB_WIFI_HOST {
    int HostKind;
    /* 1=认到；0=无棒；-1=无控制器 */
    int (*Claim)(void);
    UINT16 (*Pid)(void);
} USB_WIFI_HOST;

void UsbWifiHostRegister(const USB_WIFI_HOST *Host);
UINTN UsbWifiHostCount(void);
const USB_WIFI_HOST *UsbWifiHostGet(UINTN Index);

#endif
