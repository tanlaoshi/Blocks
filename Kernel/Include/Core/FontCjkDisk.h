/*
 * FontCjkDisk.h — 盘上 CJK32.BIN（默认 32×32×4bpp；亦兼容旧 18×18）
 *
 * 缺文件/坏魔数 → 回退内建 FontCjk16 1bpp。
 */
#ifndef FONT_CJK_DISK_H
#define FONT_CJK_DISK_H

#include "BootTypes.h"

int FontCjkDiskLoad(void);
int FontCjkDiskReady(void);
UINT32 FontCjkDiskDim(void);
/* 查到返回点阵指针；*OutBytes=每字字节数；失败 0 */
const UINT8 *FontCjkDiskLookup(UINT32 Cp, UINT32 *OutBytes);

#endif
