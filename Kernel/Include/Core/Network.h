/*
 * Network.h — 网络模块
 *
 * PCI 探卡 + HalNet（virtio-net）最小 TX/RX；lwIP/Socket 另刀。
 */
#ifndef NETWORK_H
#define NETWORK_H

#include "BootTypes.h"

int NetworkInitialize(void);
int NetworkNicReady(void);
UINT16 NetworkNicVendorId(void);
UINT16 NetworkNicDeviceId(void);

#endif
