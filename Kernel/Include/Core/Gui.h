/*
 * Gui.h — 图形界面（K12 壳 + K17 指针）
 *
 * 桌面底色/顶栏；GuiPoll 驱动光标与顶栏点击反馈。
 */
#ifndef GUI_H
#define GUI_H

int GuiInitialize(void);
/* 1 = 已画过桌面壳 */
int GuiDesktopReady(void);
/* Console 空闲时调用：吃 PS/2 鼠包、移光标、点顶栏打日志 */
void GuiPoll(void);

#endif
