/*
 * Theme.h — 主题色板（K21 最小 · 对标现网 D 族入口）
 *
 * 【初学者】
 * 只提供桌面/顶栏/字色 getter；无 THEME.CFG、无 Settings、无壁纸。
 * 名字尽量贴近现网 Theme.h，方便后刀加厚。
 */
#ifndef THEME_H
#define THEME_H

#include "BootTypes.h"

void ThemeInitialize(void);

UINT32 ThemeDesktopBackground(void);
UINT32 ThemeTaskbarBackground(void);
UINT32 ThemeWindowTitleText(void);
/* 屏上横幅 / 普通正文 */
UINT32 ThemeTextForeground(void);

#endif
