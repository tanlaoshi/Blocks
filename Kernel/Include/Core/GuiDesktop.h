/*
 * GuiDesktop.h — K33/K34：桌面图标（Shell + Settings）
 */
#ifndef GUI_DESKTOP_H
#define GUI_DESKTOP_H

#include "BootTypes.h"

void GuiDesktopSetFb(UINT32 W, UINT32 H);
void GuiDesktopPollTick(void);
void GuiDesktopPaintIcons(void);
/* -1=无；否则 GUI_WIN_SHELL / GUI_WIN_SETTINGS */
int GuiDesktopHitIcon(INT32 X, INT32 Y);
/* 双击则开/聚焦对应窗，返回 1 */
int GuiDesktopClickIcon(int WinId, INT32 X, INT32 Y);

#endif
