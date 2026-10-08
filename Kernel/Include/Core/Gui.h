/*
 * Gui.h — 图形界面（K12：桌面壳最小）
 *
 * 本刀：底色 + 顶栏 + 标题。Theme / 窗管 / 光标后刀。
 */
#ifndef GUI_H
#define GUI_H

int GuiInitialize(void);
/* 1 = 已画过桌面壳 */
int GuiDesktopReady(void);

#endif
