/*
 * Network.h — 网络模块
 *
 * PCI 探卡 + HalNet（virtio-net）最小 TX/RX；K38 ICMP ping；lwIP 另刀。
 */
#ifndef NETWORK_H
#define NETWORK_H

#include "BootTypes.h"

int NetworkInitialize(void);
int NetworkNicReady(void);
UINT16 NetworkNicVendorId(void);
UINT16 NetworkNicDeviceId(void);

/* 解析 a.b.c.d → 主机序 UINT32；成功 0 */
int NetworkParseIp(const char *S, UINT32 *Out);
/* ICMP echo；Host 空则 10.0.2.2。0=通；负=失败码 */
int NetworkPing(const char *Host, int TimeoutMs);

#endif
