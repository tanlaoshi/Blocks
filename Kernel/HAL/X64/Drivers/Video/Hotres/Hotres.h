/*
 * Hotres.h — x86 热切（Bochs DISPI / Boot GOP）；仅 HAL/X64 使用
 *
 * Common/Video 不提供本 API；Arm/RiscV HalVideo SetMode 恒失败。
 */
#ifndef VIDEO_HOTRES_H
#define VIDEO_HOTRES_H

#include "BootTypes.h"

int VideoBochsAvailable(void);
int VideoBochsSetMode(UINT32 Width, UINT32 Height);
int VideoGopAvailable(void);
int VideoGopSetMode(UINT32 Width, UINT32 Height);

#endif
