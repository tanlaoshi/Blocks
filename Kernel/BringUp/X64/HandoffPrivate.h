/*
 * HandoffPrivate.h — X64 KernelHandoff 内部分工（Config / Memory / Platform）
 */
#ifndef HANDOFF_PRIVATE_H
#define HANDOFF_PRIVATE_H

#include "UefiBootConfig.h"
#include "BootInfo.h"

int HandoffAddRegion(BOOT_INFO *Info, UINT64 Phys, UINT64 Size, int Free);

/* Video / Modes / Gop / BootDiskGuid → BOOT_INFO（不含内存图） */
void HandoffFillConfig(const UEFI_BOOT_CONFIG *Cfg, BOOT_INFO *Out);

/* XHCI / RSDP / SystemTable → HalPlatform */
void HandoffFillPlatform(const UEFI_BOOT_CONFIG *Cfg);

/* 内存图 + 保留区 → BOOT_INFO；Runtime/高址 Loader → HalPlatformNoteRuntimeRange */
void HandoffFillMemory(const UEFI_BOOT_CONFIG *Cfg, BOOT_INFO *Out,
                       const UEFI_BOOT_CONFIG *CfgPhys);

#endif
