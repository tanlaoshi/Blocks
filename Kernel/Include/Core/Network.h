/*
 * Network.h — 网络（K11：探网卡最小）
 *
 * 本刀：PCI 扫到 class=0x02 即报 ok。收发包 / lwIP 后刀。
 */
#ifndef NETWORK_H
#define NETWORK_H

#include "BootTypes.h"

int NetworkInitialize(void);
/* 1 = 已见至少一块网卡 */
int NetworkNicReady(void);
UINT16 NetworkNicVendorId(void);
UINT16 NetworkNicDeviceId(void);

#endif
