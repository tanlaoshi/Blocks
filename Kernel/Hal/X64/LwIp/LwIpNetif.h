#ifndef LWIP_NETIF_H
#define LWIP_NETIF_H

#include "BootTypes.h"

int LwIpNetifAdd(UINT32 Ip, UINT32 Mask, UINT32 Gw);
void LwIpNetifInput(const UINT8 *Frame, UINTN Len);

#endif
