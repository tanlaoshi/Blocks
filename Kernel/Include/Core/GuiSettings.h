/*
 * GuiSettings.h — K34：Settings 客户区色板
 */
#ifndef GUI_SETTINGS_H
#define GUI_SETTINGS_H

#include "BootTypes.h"

void GuiSettingsPaintClient(UINT32 Cx, UINT32 Cy, UINT32 Cw, UINT32 Ch);
/* 绝对坐标点击；1=已改色并应整桌刷新 */
int GuiSettingsClick(INT32 X, INT32 Y);

#endif
