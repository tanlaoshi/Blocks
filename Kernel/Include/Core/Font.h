/*
 * Font.h — 画字积木（K21）
 *
 * 【初学者】
 * 点阵与描字政策在本积木；屏幕门面仍是 HalVideoDrawString*（由 Font.c 实现）。
 * 后刀可换字库/TTF，调用方不用改。
 */
#ifndef FONT_H
#define FONT_H

#include "BootTypes.h"

void FontInitialize(void);
UINT32 FontCellWidth(void);
UINT32 FontCellHeight(void);

#endif
