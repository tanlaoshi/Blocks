/*
 * LibWrite.h — Library 文本输出槽（不 #include Console / Hal / Core）
 *
 * Serial 或 Console 注册写函数；未注册则空操作。
 */
#ifndef LIB_WRITE_H
#define LIB_WRITE_H

#include "BootTypes.h"

void LibWriteRegister(void (*Write)(const char *Text));
void LibWrite(const char *Text);
void LibWriteHex32(UINT32 Value);
void LibWriteHex64(UINT64 Value);

#endif
