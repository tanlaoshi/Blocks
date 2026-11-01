/*
 * HalPlatform.h — 平台形状与 Boot 交来的固件指针
 */
#ifndef HAL_PLATFORM_H
#define HAL_PLATFORM_H

#include "BootTypes.h"

void HalPlatformMapMmio(void);

/* Boot 传入的 EFI_SYSTEM_TABLE*；供 Runtime GetTime（PR-G-taskbar-clock） */
void HalPlatformSetSystemTable(void *SystemTable);
void *HalPlatformSystemTable(void);
void HalPlatformNoteRuntimeRange(UINT64 Phys, UINT64 Size);
int HalPlatformRuntimeRangeCount(void);

/*
 * VirtDesktop 模块表（对表 Kernel.c 路 2）：virt 或真机有 FB 的 Arm 板。
 * 与 IsVirtSerialConsole 拆开：后者只门控 CoopDrain / virt USB-early。
 */
int HalPlatformUseVirtDesktop(void);

/*
 * 串口协作形状：开窗 App 也 CoopDrain、定时器勿抢用户、Cpu 里挂 virtio USB。
 * 仅 QEMU virt（及同形）为真。真机有屏桌面须为 0，否则双击 Task Manager 冻 GUI。
 * 串口子集请用 HalConsoleOnly，不要把本函数当「所有非 x86」。
 */
int HalPlatformIsVirtSerialConsole(void);
void HalVirtPlatformIdleLoop(void);

#endif
