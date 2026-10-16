/*
 * UiWidgets.h — Blocks 控件：按钮、列表行、滚动条、换行文字。
 *
 * 几何在 UI.h / Gfx.h。本头不进对外几何库。
 */
#ifndef UI_WIDGETS_H
#define UI_WIDGETS_H

#include "UI.h"

void UiDrawButton(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, const char *Text, UINT32 TextColor, UINT32 BgColor);
/*
 * Hovered / Pressed。Pressed 优先于 Hovered。
 * 按下：背景加深 + 文字右下偏 1px。
 */
void UiDrawButtonEx(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, const char *Text,
                    UINT32 TextColor, UINT32 BgColor, int Hovered, int Pressed);
void UiDrawButtonDisabled(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, const char *Text);
void UiDrawProgressBar(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Progress, UINT32 MaxProgress, UINT32 Color, UINT32 BgColor);
void UiDrawListRow(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, const char *Text,
                   int Selected, int Hovered);
void UiDrawScrollBar(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height,
                     int First, int Visible, int Total);
/* 点中滚动条时更新 *First；返回 1=已处理 */
int UiScrollBarHit(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height,
                   int First, int Visible, int Total,
                   UINT32 ClickX, UINT32 ClickY, int *OutFirst);
/* 列表区 Y → 可见行号（0..Visible-1），越界返回 -1 */
int UiListRowFromY(UINT32 ListTop, UINT32 LineH, int Visible, UINT32 ClickY);
/* 点是否在矩形内（含左上、不含右下） */
int UiHitRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Px, UINT32 Py);

/*
 * UTF-8 按 MaxW 软换行绘制；遇 '\n' 硬换行。
 * MaxY=0 不限底。LineStep 为行距。返回下一空行 Y。
 */
UINT32 UiDrawTextWrap(UINT32 X, UINT32 Y, UINT32 MaxW, UINT32 MaxY,
                      UINT32 LineStep, const char *S, UINT32 Color);
/* 就地截断 UTF-8，使宽度不超过 MaxW */
void UiFitTextUtf8(char *S, UINT32 MaxW);

#endif
