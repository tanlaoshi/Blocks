/*
 * Theme.h — 主题色板（K21 getter · K34 Settings · K37 THEME.CFG）
 *
 * 【初学者】
 * 内存色板；`ThemeLoadCfg`/`ThemeSaveCfg` 读写根目录 THEME.CFG。
 * mode= 仅影响下次 Boot 选 GOP（见 Boot VideoTheme），内核不热切。
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

/* Settings / CFG 写回内存色板 */
void ThemeSetDesktopBackground(UINT32 Color);
void ThemeSetWindowTitleBar(UINT32 Color);
void ThemeSetTaskbarBackground(UINT32 Color);

/* 命名预设：ink（默认）| slate | pine；不提供 tech */
int ThemeApplyNamed(const char *Name);
const char *ThemeName(void);

/* K37：分辨率偏好（0,0=auto）；落盘在 ThemeSaveCfg */
void ThemeGetMode(UINT32 *W, UINT32 *H);
void ThemeSetMode(UINT32 W, UINT32 H);

/* 读/写 THEME.CFG；无盘或失败返回 -1，缺文件 Load 也 -1（色板保持出厂） */
int ThemeLoadCfg(void);
int ThemeSaveCfg(void);

#endif
