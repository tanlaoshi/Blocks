/*
 * IdentityMap.h — 早期内核恒等映射窗口（长期策略）
 *
 * 旧 ToyKernel x86 曾用 512MB，导致 UEFI 高址栈必须在开页表前换掉。
 * OpenBox 起统一为更大窗口，与 Arm/RiscV 早期「低 4GiB 恒等」同量级。
 *
 * 约束：
 *   - 启用自有页表后，内核直接指针访问只保证 [0, TOY_IDENTITY_BYTES)。
 *   - PMM 迁入后「直接可交页」上限须与此一致（或更大且改页表）。
 *   - 更高地址的 MMIO/FB 仍须按需 Map（与旧设计相同）。
 */
#ifndef IDENTITY_MAP_H
#define IDENTITY_MAP_H

#include "BootTypes.h"

/* 早期恒等：4GiB（2MiB 大页 × 2048） */
#define TOY_IDENTITY_MB    4096u
#define TOY_IDENTITY_BYTES (4096ull << 20)

#endif
