#ifndef LWIP_NETIF_H
#define LWIP_NETIF_H

#include "BootTypes.h"

int LwIpNetifAdd(UINT32 Ip, UINT32 Mask, UINT32 Gw);
int LwIpNetifSetAddr(UINT32 Ip, UINT32 Mask, UINT32 Gw);
/* QEMU SLIRP：为 10.0.2.x 预置 ARP（网关/DNS） */
void LwIpNetifSeedSlirp(UINT32 HostIp);
void LwIpNetifInput(const UINT8 *Frame, UINTN Len);

#endif
