/*
 * Debug.h — 内核调试输出开关（KERNEL_DEBUG）与串口总开关（SERIAL_ENABLE）正交
 *
 * 编译：make DEBUG=1 或 ./build.sh DEBUG=1 → -DKERNEL_DEBUG=1
 * 默认 KERNEL_DEBUG=0：DebugWrite/DebugHex* 编译为空操作。
 * 打开时实现在 Core/Debug.c，由那里再进串口。
 * 串口总开关 / 分模块：见 SerialConfig.h（SERIAL=0 / SERIAL_USB=0 …）。
 * Shell 命令输出请继续用 gConsole.Write（不受此开关影响）。
 */
#ifndef KERNEL_DEBUG_H
#define KERNEL_DEBUG_H

#include "BootTypes.h"

#ifndef KERNEL_DEBUG
#define KERNEL_DEBUG 0
#endif

#if KERNEL_DEBUG
void DebugWrite(const char *Text);
void DebugHex32(UINT32 Value);
void DebugHex64(UINT64 Value);
#else
#define DebugWrite(Text)   ((void)0)
#define DebugHex32(Value)  ((void)(Value))
#define DebugHex64(Value)  ((void)(Value))
#endif

#endif
