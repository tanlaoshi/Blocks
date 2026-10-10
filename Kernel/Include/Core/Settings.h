/*
 * Settings.h — K34：Settings 客户区色板
 */
#ifndef SETTINGS_H
#define SETTINGS_H

#include "BootTypes.h"

void SettingsPaintClient(UINT32 Cx, UINT32 Cy, UINT32 Cw, UINT32 Ch);
/* 绝对坐标点击；1=已改色并应整桌刷新 */
int SettingsClick(INT32 X, INT32 Y);

#endif
