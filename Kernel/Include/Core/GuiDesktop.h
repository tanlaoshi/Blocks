/*
 * GuiDesktop.h — K33：桌面图标（最小：Shell 一枚 + 双击）
 */
#ifndef GUI_DESKTOP_H
#define GUI_DESKTOP_H

#include "BootTypes.h"

void GuiDesktopSetFb(UINT32 W, UINT32 H);
/* 每拍 GuiPoll 调一次：无硬件 tick 时作双击软时钟 */
void GuiDesktopPollTick(void);
void GuiDesktopPaintIcons(void);
/* 1 = 点在 Shell 图标上 */
int GuiDesktopHitShell(INT32 X, INT32 Y);
/*
 * 单击记时；若构成双击则开/聚焦 Shell 并返回 1。
 * 调用方已确认 Hit 桌面且落在图标上。
 */
int GuiDesktopClickShell(INT32 X, INT32 Y);

#endif
