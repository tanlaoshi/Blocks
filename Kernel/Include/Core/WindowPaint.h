/*
 * WindowPaint.h — Window 绘制帮手（仅 Window*.c）
 */
#ifndef WINDOW_PAINT_H
#define WINDOW_PAINT_H

#include "BootTypes.h"
#include "Locale.h"

void WindowPaintCloseButton(UINT32 WinX, UINT32 WinY, UINT32 WinW,
                          UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H);
void WindowPaintDesktopFramebuffer(UINT32 FbW, UINT32 FbH);
void WindowPaintErase(UINT32 FbW, UINT32 FbH, UINT32 X, UINT32 Y, UINT32 W,
                       UINT32 H);
void WindowPaintPresentMove(UINT32 FbW, UINT32 FbH, UINT32 X0, UINT32 Y0,
                             UINT32 W0, UINT32 H0, UINT32 X1, UINT32 Y1,
                             UINT32 W1, UINT32 H1);
/* Kind = GUI_WIN_* ：Shell 横幅 / About 文案 / Settings 色板 */
void WindowPaintFrame(UINT32 X, UINT32 Y, UINT32 W, UINT32 H, int Focus,
                       LOC_MSG Title, int Kind);

#endif
