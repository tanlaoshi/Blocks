/*
 * EarlyIdentity.h — X64 早期自建页表（在完整 VMM/PMM 之前）
 *
 * 【初学者】
 * Setup  = 在静态池里搭好 PML4→PDPT→PD（2MiB 大页铺满恒等窗）
 * Enable = 写 CR4.PAE、CR3、CR0.PG，真正「打开分页」
 * Root   = 返回页表根物理址（调试 / 以后交接正式 VMM）
 *
 * 窗口大小见 IdentityMap.h（TOY_IDENTITY_BYTES = 4GiB）。
 */
#ifndef EARLY_IDENTITY_H
#define EARLY_IDENTITY_H

#include "BootTypes.h"

int EarlyIdentitySetup(void);
void EarlyIdentityEnable(void);
UINT64 EarlyIdentityRoot(void);

#endif
