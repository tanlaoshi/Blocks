/*
 * Rtl8188eu.h — USB RTL8188EU 对外 API（PR-N-wifi-1）
 *
 * 探针 / 可选固件钩子；wifi-2 再挂 Net。
 */
#ifndef WIFI_H
#define WIFI_H

#include "Rtl8188eu/WifiIds.h"
int WifiSetup(void);
int WifiReady(void);
void WifiGetMac(UINT8 Mac[6]);
UINT16 WifiUsbPid(void);
/* 1=xhci 2=ehci 0=无 */
int WifiHostKind(void);
int WifiFwLoaded(void);

#endif
