/*
 * Font.h — 画字积木契约（K21 落点；声明/实现对齐）
 *
 * 【初学者】
 * 桌面画字积木。帧缓冲门面是 HalVideo（DrawPixel/FillRect…）。
 * 开机屏上滚用 HAL 自持 HalBootFont，与本积木分离、可日后换成另一套。
 */
#ifndef FONT_H
#define FONT_H

#include "BootTypes.h"

void FontInitialize(void);
UINT32 FontCellWidth(void);
UINT32 FontCellHeight(void);

void FontDrawCharAt(UINT32 X, UINT32 Y, char C, UINT32 Color);
void FontDrawStringAt(UINT32 X, UINT32 Y, const char *Text, UINT32 Color);
void FontDrawChar(char C, UINT32 Color);
void FontDrawString(const char *Text, UINT32 Color);

#endif
