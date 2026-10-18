/*
 * Private.h — Hub 扫描和认领共用的路由、槽。定义在 Wifi.c。
 * gWifiSlot / gWifiPort / gWifiXhciOk 已在 XHCI/Private.h，这里不重复。
 */
#ifndef XHCI_WIFI_PRIVATE_H
#define XHCI_WIFI_PRIVATE_H

#include "XHCI/Private.h"

extern UINT32 gWifiRoute;
extern UINT8 gWifiHubSlot;
extern UINT8 gWifiTtPort;
extern UINT8 gWifiDevCtx[2048];

#endif
