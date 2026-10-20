/*
 * StoreUi.h — 商店 GUI 三分栏（story-s4）
 *
 * 开窗：GuiOpenStore → StoreUiOpen（defer）→ Worker StoreUiEnsureOpenLoad
 * 钮：Input → StoreJobEnqueue；泵：Worker StoreJobStep（StoreUiPump 空壳不 Step）
 * 左 All/Apps/Fonts/Assets/Installed；中列表；右详情 + Install/Remove/Sync/Repo
 */
#ifndef STORE_UI_H
#define STORE_UI_H

#include "BootTypes.h"

void StoreUiOpen(void);
/* Worker：开窗后补 catalog/已装探测（勿在点击路径同步扫盘） */
void StoreUiEnsureOpenLoad(void);
void StoreUiPaintFocused(void);
void StoreUiRepaint(void);
void StoreUiOnClick(UINT32 X, UINT32 Y);
/* 与 Settings 一致：悬停/按下高亮，抬起触发 */
void StoreUiOnPointer(UINT32 X, UINT32 Y, UINT8 Buttons);
void StoreUiOnEscape(void);
/* GuiPollMouse 末占位；Job 只在 Worker StoreJobStep（勿在此 Step） */
void StoreUiPump(void);
int StoreUiIsFocused(void);
/* 作业排队或执行中：跳过悬停重绘，只让光标跟着走 */
int StoreUiIsBusy(void);

#endif
