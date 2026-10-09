/*
 * GuiWin.h — 窗槽 + Z 序（K32 双窗 · K34 +Settings）
 */
#ifndef GUI_WIN_H
#define GUI_WIN_H

#include "BootTypes.h"

#define GUI_WIN_SHELL    0
#define GUI_WIN_ABOUT    1
#define GUI_WIN_SETTINGS 2
#define GUI_WIN_FILES    3
#define GUI_WIN_COUNT    4

void GuiWinSetFb(UINT32 W, UINT32 H);
void GuiWinLayoutAll(void);
void GuiWinPaintDesktop(void);
/* 按 Z 序画全部可见窗；Shell 客户区再画 Console 横幅 */
void GuiWinCompose(void);
void GuiWinPresentFull(void);

/* 顶层命中：-1=无 */
int GuiWinHit(INT32 X, INT32 Y);
int GuiWinInClose(int Id, INT32 X, INT32 Y);
int GuiWinInTitle(int Id, INT32 X, INT32 Y);
int GuiWinIsOn(int Id);
int GuiWinFocused(int Id);

void GuiWinRaise(int Id);
void GuiWinFocus(int Id);
void GuiWinUnfocusAll(void);
void GuiWinClose(int Id);
void GuiWinOpen(int Id);
void GuiWinMoveTo(int Id, INT32 X, INT32 Y, int RefreshShellText);
int GuiWinShellClientRect(UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H);
int GuiWinGetPos(int Id, INT32 *X, INT32 *Y);
/* 点顶栏：打开所有已关的窗 */
void GuiWinOpenMissing(void);

#endif
