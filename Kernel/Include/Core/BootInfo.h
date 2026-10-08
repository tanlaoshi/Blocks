/*
 * BootInfo.h — Core 侧开机说明书仓储 API
 *
 * 布局见 Abi/BootInfoTypes.h。
 *
 * 【谁写 / 谁读】
 *   Handoff：在本地填 BOOT_INFO，以参数交给 KernelMain（不碰本仓）
 *   KernelMain：BootInfoStore(Info) 拷进 BSS
 *   Core 模块：BootInfoGet() 读全局仓
 *   Hal：不调用 Store/Get；能力旗标由 KernelMain 观察后注入
 */
#ifndef BOOT_INFO_H
#define BOOT_INFO_H

#include "BootInfoTypes.h"

/* 把 Handoff 交来的说明书拷进内核 BSS（不是字段 setter） */
void BootInfoStore(const BOOT_INFO *Info);
const BOOT_INFO *BootInfoGet(void);
VIDEO_CONFIG BootInfoToVideoConfig(const BOOT_INFO *Info);

#endif
