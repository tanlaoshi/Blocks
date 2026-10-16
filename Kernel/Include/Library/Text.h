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

#endif
