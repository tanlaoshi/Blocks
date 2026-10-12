/*
 * IdentityMap.h — 早期恒等映射窗口（策略常量）
 *
 * X64 EarlyIdentity / VMM / PMM 直接指针上限应对齐本值。
 */
#ifndef IDENTITY_MAP_H
#define IDENTITY_MAP_H

#include "BootTypes.h"

/* 早期恒等：4GiB（2MiB 大页） */
#define IDENTITY_MB    4096u
#define IDENTITY_BYTES (4096ull << 20)

#endif
