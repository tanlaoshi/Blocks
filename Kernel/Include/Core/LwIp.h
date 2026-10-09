/*
 * LwIp.h — lwIP 门面（未编 HAVE_LWIP 时桩失败）
 */
#ifndef LWIP_H
#define LWIP_H

#include "BootTypes.h"

int LwIpInitialize(void);
int LwIpActive(void);
void LwIpService(void);
int LwIpPing(UINT32 DstIp, int TimeoutMs);
/* 按 NetConfig 刷新 netif/DNS；未 on 时 0 */
int LwIpApplyConfig(void);
/* 点分字面量或 DNS A；成功 0 */
int LwIpDnsLookup(const char *Name, UINT32 *OutIp, int TimeoutMs);

#endif
