/*
 * BootConfig.h — X64 HAL 侧对 Boot/BootPkg/UefiBootConfig.h 的薄包装
 *
 * 权威布局在 BootPkg；此处提供 BOOT_CONFIG 别名供 KernelHandoff.c 使用。
 */
#ifndef BOOT_CONFIG_H
#define BOOT_CONFIG_H

#include "UefiBootConfig.h"
#include "BootInfo.h"

typedef UEFI_BOOT_CONFIG BOOT_CONFIG;
typedef TOY_MEMORY_MAP  MEMORY_MAP;

#if defined(__GNUC__)
_Static_assert(sizeof(VIDEO_CONFIG) == sizeof(TOY_VIDEO_CONFIG), "VIDEO_CONFIG ABI");
#endif

#endif
