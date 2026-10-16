/*
 * Text.h — 数字写成文本。不依赖 PCI 或串口。
 *
 * 三个旧名仍返回静态缓冲，字符与从 PCIe.c 搬来时相同。
 * 同一次调用里写两次会共用一块缓冲。
 */
#ifndef TEXT_H
#define TEXT_H

#include "BootTypes.h"

char *Uint32ToHex(UINT32 Value);
char *Uint8ToDecimal(UINT8 Value);
char *Uint64ToHex(UINT64 Value);

/* Digits 个半字节。Uppercase 非 0 用 A-F，否则 a-f。WithPrefix 非 0 先写 "0x"。
   放不下则当 Capacity > 0 且 Buffer 非空时写成空串，不写出界。 */
void FormatHexadecimal(char *Buffer, int Capacity, UINT64 Value,
                       int Digits, int Uppercase, int WithPrefix);

/* 低 4 位，小写一个字符 */
char FormatHexadecimalDigit(UINT32 Value);

#endif
