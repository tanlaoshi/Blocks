/*
 * Configuration.h — K42：可配 IPv4 / 网关 / DNS
 *
 * QEMU user 默认 10.0.2.15/24 gw=10.0.2.2 dns=10.0.2.3。
 */
#ifndef NET_CONFIG_H
#define NET_CONFIG_H

#include "BootTypes.h"

void ConfigurationEnsure(void);
UINT32 ConfigurationGetIp(void);
UINT32 ConfigurationGetMask(void);
UINT32 ConfigurationGetGw(void);
UINT32 ConfigurationGetDns(void);

int ConfigurationSetIp(UINT32 Ip);
int ConfigurationSetMask(UINT32 Mask);
int ConfigurationSetGw(UINT32 Gw);
int ConfigurationSetDns(UINT32 Dns);

/* 点分文本到 Buf（至少 16 字节）；失败写 "?" */
void ConfigurationFormatIp(UINT32 Ip, char *Buf, int Cap);

#endif
