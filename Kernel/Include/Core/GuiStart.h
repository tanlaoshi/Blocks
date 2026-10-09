/*
 * GuiStart.h — K36：任务栏开始钮 + 弹出菜单
 */
#ifndef GUI_START_H
#define GUI_START_H

#include "BootTypes.h"

void GuiStartSetFb(UINT32 W, UINT32 H);
/* 画底栏 + 左侧开始钮（顶栏标题仍由 GuiWinPaint） */
void GuiStartPaintBar(void);
/* 菜单开时画在窗之上 */
void GuiStartPaintMenu(void);
int GuiStartMenuOpen(void);
void GuiStartCloseMenu(void);
/* 1=命中开始钮 */
int GuiStartHitButton(INT32 X, INT32 Y);
/* 命中菜单项：GUI_WIN_*；未命中 -1；点在菜单空白 -2 */
int GuiStartHitMenu(INT32 X, INT32 Y);
/* 切换菜单；返回 1=需要整桌刷新 */
int GuiStartToggle(void);
/* 选中菜单项开窗并关菜单；1=已处理 */
int GuiStartActivate(int WinId);

#endif
