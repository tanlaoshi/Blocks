/*
 * Dtb.h — 最小设备树（FDT）解析声明
 *
 * 【初学者】
 * Arm/RiscV 开机时常有一份 Flattened Device Tree，描述 RAM/UART/CPU…
 * Common 不解析 DTB；只由 Hal/Handoff 填进 BOOT_INFO。
 * 本头是共享声明；极简实现可在各 Arch 的 KernelHandoff.c 内，
 * 或以后抽到独立 .c。
 */
#ifndef HAL_DTB_H
#define HAL_DTB_H

#include "BootTypes.h"

/* 从 FDT 取第一块 memory 的 reg；成功 0，失败 -1 */
int DtbMemoryRegion(UINT64 DtbPhys, UINT64 *OutBase, UINT64 *OutSize);

/* 找 compatible=qemu,fw-cfg-mmio 的 reg 基址；成功 0，失败 -1 */
int DtbFwCfgBase(UINT64 DtbPhys, UINT64 *OutBase);

/* 统计 FDT 中 cpu@* 节点数（��；失败返回 1 */
int DtbCpuCount(UINT64 DtbPhys);

#endif
