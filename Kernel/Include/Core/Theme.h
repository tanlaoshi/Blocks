/*
 * Theme.h — 主题色板（K21 getter · K34 Settings 可写回）
 *
 * 【初学者】
 * 内存色板；THEME.CFG 落盘见 K37。
 */
#ifndef THEME_H
#define THEME_H

#include "BootTypes.h"

void ThemeInitialize(void);

UINT32 ThemeDesktopBackground(void);
UINT32 ThemeTaskbarBackground(void);
UINT32 ThemeWindowTitleText(void);
UINT32 ThemeTextForeground(void);
UINT32 ThemeWindowTitleBar(void);
UINT32 ThemeWindowTitleBarDim(void);
UINT32 ThemeWindowClient(void);
UINT32 ThemeWindowBorder(void);

/* K34：Settings 写回（立即；不落盘） */
void ThemeSetDesktopBackground(UINT32 Color);
void ThemeSetWindowTitleBar(UINT32 Color);
void ThemeSetTaskbarBackground(UINT32 Color);

#endif

