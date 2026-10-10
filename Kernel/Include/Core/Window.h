/*
 * Window.h — 窗槽 + Z 序（K32 双窗 · K34 +Settings）
 */
#ifndef WINDOW_H
#define WINDOW_H

#include "BootTypes.h"

#define GUI_WIN_SHELL    0
#define GUI_WIN_ABOUT    1
#define GUI_WIN_SETTINGS 2
#define GUI_WIN_FILES    3
#define GUI_WIN_STORE    4
#define GUI_WIN_COUNT    5

void WindowSetFb(UINT32 W, UINT32 H);
void WindowLayoutAll(void);
void WindowPaintDesktop(void);
/* 按 Z 序画全部可见窗；Shell 客户区再画 Console 横幅 */
void WindowCompose(void);
void WindowPresentFull(void);

/* 顶层命中：-1=无 */
int WindowHit(INT32 X, INT32 Y);
int WindowInClose(int Id, INT32 X, INT32 Y);
int WindowInTitle(int Id, INT32 X, INT32 Y);
int WindowIsOn(int Id);
int WindowFocused(int Id);

void WindowRaise(int Id);
void WindowFocus(int Id);
void WindowUnfocusAll(void);
void WindowClose(int Id);
void WindowOpen(int Id);
void WindowMoveTo(int Id, INT32 X, INT32 Y, int RefreshShellText);
int WindowShellClientRect(UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H);
int WindowGetPos(int Id, INT32 *X, INT32 *Y);
/* 点顶栏：打开所有已关的窗 */
void WindowOpenMissing(void);

#endif
