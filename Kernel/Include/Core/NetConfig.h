/*
 * NetConfig.h — K42：可配 IPv4 / 网关 / DNS
 *
 * QEMU user 默认 10.0.2.15/24 gw=10.0.2.2 dns=10.0.2.3。
 */
#ifndef NET_CONFIG_H
#define NET_CONFIG_H

#include "BootTypes.h"

void NetConfigEnsure(void);
UINT32 NetConfigGetIp(void);
UINT32 NetConfigGetMask(void);
UINT32 NetConfigGetGw(void);
UINT32 NetConfigGetDns(void);

int NetConfigSetIp(UINT32 Ip);
int NetConfigSetMask(UINT32 Mask);
int NetConfigSetGw(UINT32 Gw);
int NetConfigSetDns(UINT32 Dns);

/* 点分文本到 Buf（至少 16 字节）；失败写 "?" */
void NetConfigFormatIp(UINT32 Ip, char *Buf, int Cap);

#endif
