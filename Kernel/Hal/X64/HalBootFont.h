/*
 * HalBootFont.h — HAL 启动屏点阵字（仅 HalSerialGop 等 early 路径）
 *
 * 【初学者】
 * 与 Core/Font 桌面字库分离：开机 SCREEN_LOG 自持一份，不回调 Core。
 * 进桌面后 Gui 走 FontDraw*，可换成另一套方案。
 */
#ifndef HAL_BOOT_FONT_H
#define HAL_BOOT_FONT_H

#include "BootTypes.h"

UINT32 HalBootFontCellHeight(void);
void HalBootFontDrawStringAt(UINT32 X, UINT32 Y, const char *Text, UINT32 Color);

#endif
