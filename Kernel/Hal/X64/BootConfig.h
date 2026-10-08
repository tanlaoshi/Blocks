/*
 * BootConfig.h — 让 X64 KernelHandoff 能看见 Boot 柱的交接结构
 *
 * 【初学者】
 * 权威布局在 Boot/BootPkg/UefiBootConfig.h（ToyBoot 填写）。
 * Kernel 侧不复制一份结构体，只：
 *   1) include 那个头（编译时 -I Boot/BootPkg）
 *   2) typedef 成历史上的名字 BOOT_CONFIG / MEMORY_MAP，少改 Handoff 代码
 *
 * VIDEO_CONFIG（内核）与 TOY_VIDEO_CONFIG（Boot）大小必须一致，
 * 下面 static_assert 就是在防 ABI 漂移。
 */
#ifndef BOOT_CONFIG_H
#define BOOT_CONFIG_H

#include "UefiBootConfig.h"
#include "BootInfoTypes.h"

typedef UEFI_BOOT_CONFIG BOOT_CONFIG;
typedef TOY_MEMORY_MAP  MEMORY_MAP;

#if defined(__GNUC__)
_Static_assert(sizeof(VIDEO_CONFIG) == sizeof(TOY_VIDEO_CONFIG), "VIDEO_CONFIG ABI");
#endif

#endif
