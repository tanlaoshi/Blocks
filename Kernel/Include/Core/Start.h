/*
 * Start.h — K36：任务栏开始钮 + 弹出菜单
 */
#ifndef START_H
#define START_H

#include "BootTypes.h"

void StartSetFb(UINT32 W, UINT32 H);
/* 画底栏 + 左侧开始钮（顶栏标题仍由 WindowPaint） */
void StartPaintBar(void);
/* 菜单开时画在窗之上 */
void StartPaintMenu(void);
int StartMenuOpen(void);
void StartCloseMenu(void);
/* 1=命中开始钮 */
int StartHitButton(INT32 X, INT32 Y);
/* 命中菜单项：GUI_WIN_*；未命中 -1；点在菜单空白 -2 */
int StartHitMenu(INT32 X, INT32 Y);
/* 切换菜单；返回 1=需要整桌刷新 */
int StartToggle(void);
/* 选中菜单项开窗并关菜单；1=已处理 */
int StartActivate(int WindowId);

#endif
