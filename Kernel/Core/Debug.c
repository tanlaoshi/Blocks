/*
 * Debug.c — 调试打开时把字符串和数字送进串口。
 * 默认关闭时本文件不产生符号，调用处仍是空宏。
 */
#include "Debug.h"

#if KERNEL_DEBUG

#include "Hal.h"

void DebugWrite(const char *Text) {
    HalDebugWrite(Text);
}

void DebugHex32(UINT32 Value) {
    HalDebugWriteHex32(Value);
}

void DebugHex64(UINT64 Value) {
    HalDebugHex64(Value);
}

#endif
