/*
 * LwIp.h — K41：lwIP 门面（未编 HAVE_LWIP 时桩失败）
 */
#ifndef LWIP_H
#define LWIP_H

#include "BootTypes.h"

int LwIpInitialize(void);
int LwIpActive(void);
void LwIpService(void);
int LwIpPing(UINT32 DstIp, int TimeoutMs);

#endif
