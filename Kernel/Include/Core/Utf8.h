/*
 * Utf8.h — K26：最小 UTF-8 解码（码点）
 */
#ifndef UTF8_H
#define UTF8_H

#include "BootTypes.h"

/* 从 S 解一个码点到 *OutCp；返回消费字节数；失败 0 */
UINTN Utf8Decode(const char *S, UINT32 *OutCp);

#endif
