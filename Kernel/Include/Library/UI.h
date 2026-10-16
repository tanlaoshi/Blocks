/*
 * UI.h — 几何包装与颜色。线、矩形、圆的算法在 Gfx.h。
 *
 * 按钮、列表、滚动条在 UiWidgets.h。颜色为 0x00RRGGBB。
 */
#ifndef UI_H
#define UI_H

#include "BootTypes.h"

#define COLOR_BLACK       0x00000000
#define COLOR_WHITE       0x00FFFFFF
#define COLOR_RED         0x00FF0000
#define COLOR_GREEN       0x0000FF00
#define COLOR_BLUE        0x000000FF
#define COLOR_YELLOW      0x00FFFF00
#define COLOR_CYAN        0x0000FFFF
#define COLOR_MAGENTA     0x00FF00FF
#define COLOR_GRAY        0x00808080
#define COLOR_DARK_GRAY   0x00404040
#define COLOR_LIGHT_GRAY  0x00C0C0C0

void UiDrawLine(UINT32 X1, UINT32 Y1, UINT32 X2, UINT32 Y2, UINT32 Color);
void UiDrawRectangle(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color);
void UiFillRectangle(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color);
/* 半透明留在 Services，不进 Library/Gfx */
void UiFillRectangleAlpha(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height,
                          UINT32 Color, UINT8 Alpha);
UINT32 UiBlendRgb(UINT32 Dst, UINT32 Src, UINT8 Alpha);
void UiDrawRoundRectangle(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Radius, UINT32 Color);
void UiFillRoundRectangle(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Radius, UINT32 Color);
void UiDrawCircle(UINT32 CenterX, UINT32 CenterY, UINT32 Radius, UINT32 Color);
void UiFillCircle(UINT32 CenterX, UINT32 CenterY, UINT32 Radius, UINT32 Color);
void UiDrawTriangle(UINT32 X1, UINT32 Y1, UINT32 X2, UINT32 Y2, UINT32 X3, UINT32 Y3, UINT32 Color);
void UiFillTriangle(UINT32 X1, UINT32 Y1, UINT32 X2, UINT32 Y2, UINT32 X3, UINT32 Y3, UINT32 Color);

#endif
