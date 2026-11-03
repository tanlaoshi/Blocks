/*
 * Dtb.h — 最小 FDT 解析（HAL 内；Common 不碰 DTB，PR-V1）
 */
#ifndef HAL_DTB_H
#define HAL_DTB_H

#include "BootTypes.h"

/* 从 FDT 取第一块 memory 的 reg；成功 0，失败 -1 */
int DtbMemoryRegion(UINT64 DtbPhys, UINT64 *OutBase, UINT64 *OutSize);

/* 找 compatible=qemu,fw-cfg-mmio 的 reg 基址；成功 0，失败 -1 */
int DtbFwCfgBase(UINT64 DtbPhys, UINT64 *OutBase);

/* 统计 FDT 中 cpu@* 节点数（PR-A14）；失败返回 1 */
int DtbCpuCount(UINT64 DtbPhys);

/* 首个 cpu@* 的 enable-method 字符串；成功 0，失败 -1（Out 空串） */
int DtbCpuEnableMethod(UINT64 DtbPhys, char *Out, int Capacity);

/* FDT magic 是否像有效 blob（memory 模板坏时仍可用于 cpu 节点） */
int DtbLooksValid(UINT64 DtbPhys);

/*
 * 第 CpuIndex 个 cpu@* 的 cpu-release-addr（spin-table）。
 * 成功 0；无属性 / 越界 -1。
 */
int DtbCpuReleaseAddress(UINT64 DtbPhys, UINT32 CpuIndex, UINT64 *OutPhys);

#endif
