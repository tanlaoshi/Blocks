/*
 * Desktop.h — K33/K34：桌面图标（Shell + Settings）
 */
#ifndef DESKTOP_H
#define DESKTOP_H

#include "BootTypes.h"

void DesktopSetFb(UINT32 W, UINT32 H);
void DesktopPollTick(void);
void DesktopPaintIcons(void);
/* -1=无；否则 GUI_WIN_SHELL / GUI_WIN_SETTINGS */
int DesktopHitIcon(INT32 X, INT32 Y);
/* 双击则开/聚焦对应窗，返回 1 */
int DesktopClickIcon(int WinId, INT32 X, INT32 Y);

#endif
