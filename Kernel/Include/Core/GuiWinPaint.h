/*
 * GuiWinPaint.h — GuiWin 绘制帮手（仅 GuiWin*.c）
 */
#ifndef GUI_WIN_PAINT_H
#define GUI_WIN_PAINT_H

#include "BootTypes.h"
#include "Locale.h"

void GuiWinPaint_CloseBtn(UINT32 WinX, UINT32 WinY, UINT32 WinW,
                          UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H);
void GuiWinPaint_Desktop(UINT32 FbW, UINT32 FbH);
void GuiWinPaint_Erase(UINT32 FbW, UINT32 FbH, UINT32 X, UINT32 Y, UINT32 W,
                       UINT32 H);
void GuiWinPaint_PresentMove(UINT32 FbW, UINT32 FbH, UINT32 X0, UINT32 Y0,
                             UINT32 W0, UINT32 H0, UINT32 X1, UINT32 Y1,
                             UINT32 W1, UINT32 H1);
/* KindShell=1：客户区走 Console 横幅；否则 About 文案 */
void GuiWinPaint_Frame(UINT32 X, UINT32 Y, UINT32 W, UINT32 H, int Focus,
                       LOC_MSG Title, int KindShell);

#endif
