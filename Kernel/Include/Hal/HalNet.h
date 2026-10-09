/*
 * HalNet.h — 网卡门面（K20 最小）
 *
 * X64：legacy virtio-net；其它 Arch：stub。
 */
#ifndef HAL_NET_H
#define HAL_NET_H

#include "BootTypes.h"

int HalNetInit(void);
int HalNetReady(void);
void HalNetGetMac(UINT8 Mac[6]);
/* 发一帧以太网载荷（不含 virtio_net_hdr）；成功 0 */
int HalNetTransmit(const void *Frame, UINT32 Len);
/* 非阻塞收一帧到 Buf；成功返回长度，无数据 0，错 -1 */
int HalNetReceive(void *Buf, UINT32 Cap);

#endif
