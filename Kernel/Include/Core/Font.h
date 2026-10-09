/*
 * Font.h — 画字积木（K21 ASCII；K26 UTF-8 / CJK16）
 *
 * 开机屏仍用 HalBootFont。桌面/Gui 走本积木。
 */
#ifndef FONT_H
#define FONT_H

#include "BootTypes.h"

void FontInitialize(void);
UINT32 FontCellWidth(void);   /* ASCII 步进（16） */
UINT32 FontCellHeight(void);  /* 行高（16） */
UINT32 FontCjkCell(void);     /* CJK 边长（16） */

void FontDrawCharAt(UINT32 X, UINT32 Y, char C, UINT32 Color);
void FontDrawCodepointAt(UINT32 X, UINT32 Y, UINT32 Cp, UINT32 Color);
void FontDrawStringAt(UINT32 X, UINT32 Y, const char *Text, UINT32 Color);
void FontDrawChar(char C, UINT32 Color);
void FontDrawString(const char *Text, UINT32 Color);

#endif
