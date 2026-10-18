/*
 * IpInput.h — 以太网头拆到 ICMP/UDP/TCP 的入口。
 * x64 的 PCI 网卡和虚拟机的 MMIO virtio 都调它。
 */
#ifndef NET_IP_INPUT_H
#define NET_IP_INPUT_H

#include "BootTypes.h"

void IpInput(const UINT8 *Pkt, UINTN Len);

#endif
