/*
 * EarlyIdentity.h — X64 早期自建页表（PMM/VMM 之前）
 *
 * Setup = 静态池搭 PML4→PDPT→PD；Enable = 开 PAE/CR3/PG。
 * 窗口见 IdentityMap.h（4GiB）。
 */
#ifndef EARLY_IDENTITY_H
#define EARLY_IDENTITY_H

#include "BootTypes.h"

int EarlyIdentitySetup(void);
void EarlyIdentityEnable(void);
UINT64 EarlyIdentityRoot(void);

#endif
