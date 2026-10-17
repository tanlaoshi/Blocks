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
 * virt 平台形状（协作调度、virt 桌面模块表 vs x86 全表）。
 * 串口子集请用 HalConsoleOnly，不要把本函数当「所有非 x86」。
 */
int HalPlatformIsVirtSerialConsole(void);
void HalVirtPlatformIdleLoop(void);

#endif
