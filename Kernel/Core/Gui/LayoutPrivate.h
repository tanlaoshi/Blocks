/*
 * LayoutPrivate.h — Layout.c 与 LayoutConfiguration.c 共享的设计表/CFG 可写字段
 *
 * 勿进 Include/Core；仅 Gui 目录内 TU 使用。
 */
#ifndef LAYOUT_PRIVATE_H
#define LAYOUT_PRIVATE_H

#include "Layout.h"
#include "Window.h"

extern GUI_WIN_LAYOUT gWin[GUI_WIN_COUNT];
extern UINT32 gBarH;
extern UINT32 gTitleH;
extern UINT32 gIconTile;
extern UINT32 gIconStride;

#endif
