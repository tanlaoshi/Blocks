#ifndef TOY_NETIF_H
#define TOY_NETIF_H

#include "BootTypes.h"

struct netif;

struct netif *LwIpNetifGet(void);
int LwIpNetifAdd(UINT32 Ip, UINT32 Mask, UINT32 Gw);
/* 已 Add 后改地址；成功 0 */
int LwIpNetifSetAddr(UINT32 Ip, UINT32 Mask, UINT32 Gw);
/* L2 换卡后刷新 netif MAC（读 HalNetGetMac）；WiFi 武装刀可直接写 hwaddr */
void LwIpNetifSyncMac(void);
void LwIpNetifInput(const UINT8 *Frame, UINTN Len);

#endif
