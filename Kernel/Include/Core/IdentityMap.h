/*
 * IdentityMap.h — 早期「恒等映射」窗口大小（长期策略常量）
 *
 * 【初学者 · 什么是恒等映射？】
 * 开页表之后，CPU 用的是「虚拟地址」。若某段虚址 V 映射到同样数值的物理址 P
 *（V == P），就叫恒等映射。早期内核常把低端一大段做成恒等，这样：
 *   - 内核代码/数据仍按链接时的物理地址访问；
 *   - 串口 MMIO、部分帧缓冲若落在窗内，也可先直接解引用。
 *
 * 【为什么是 4GiB？】
 * UEFI 交给我们的栈往往在很高的物理地址；若恒等窗太小，一开页表
 * 栈会「掉出窗口」。窗口取 4GiB，与 Arm64/RiscV virt 早期习惯同量级。
 *
 * 【谁读这个头？】
 *   - X64 EarlyIdentity（建 4GiB 大页表）
 *   - VirtualMemory / PMM 的「直接可交页」上限应对齐本常量
 *
 * 更高地址的 MMIO / 大帧缓冲仍要按需 Map。
 */
#ifndef IDENTITY_MAP_H
#define IDENTITY_MAP_H

#include "BootTypes.h"

/* 早期恒等：4GiB = 4096 MiB（用 2MiB 大页铺） */
#define TOY_IDENTITY_MB    4096u
#define TOY_IDENTITY_BYTES (4096ull << 20)

#endif
