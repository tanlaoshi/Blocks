/*
 * Device.h — 设备管理器门面（K5 最小壳）
 *
 * 【初学者】
 * 以后驱动通过 Device 注册/枚举；本刀只提供 Initialize / Enumerate 空壳，
 * 不 Probe 真硬件（VMM 前扫 xHCI/PS2 易踩坑）。
 */
#ifndef DEVICE_H
#define DEVICE_H

void DeviceInitialize(void);
void DeviceEnumerateAll(void);

#endif
