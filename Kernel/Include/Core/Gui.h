/*
 * Gui.h — 图形界面（K29–K32：单窗→拖焦点→关窗→双窗 Z 序）
 */
#ifndef GUI_H
#define GUI_H

#include "BootTypes.h"

int GuiInitialize(void);
int GuiDesktopReady(void);
int GuiShellWindowReady(void);
int GuiShellClientRect(UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H);
/* 1 = Shell 窗有焦点 */
int GuiShellFocused(void);
void GuiRefreshLabels(void);
/* Present/重绘前后配对：避免 XOR 光标被脏矩形啃成横线残影 */
void GuiCursorHide(void);
void GuiCursorShow(void);
int GuiPoll(void);

#endif
