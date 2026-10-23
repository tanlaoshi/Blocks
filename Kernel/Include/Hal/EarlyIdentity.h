/*
 * EarlyIdentity.h — X64 早期自建页表（PMM/VMM 之前）
 *
 * EnableEarlyIdentityMap：静态池搭 PML4→PDPT→PD（4GiB 恒等）并开 PAE/CR3/PG。
 * 窗口见 IdentityMap.h。失败则不开分页（保持调用前映射）。
 */
#ifndef EARLY_IDENTITY_H
#define EARLY_IDENTITY_H

#include "BootTypes.h"

void EnableEarlyIdentityMap(void);
UINT64 EarlyIdentityRoot(void);

#endif
