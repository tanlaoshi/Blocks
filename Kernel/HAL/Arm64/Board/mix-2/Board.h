/*
 * Board.h — MIX 2 板包门面
 */
#ifndef BOARD_H
#define BOARD_H

#include "BoardConfig.h"
#include "BootTypes.h"
#include "BootHandoff.h"

const char *BoardName(void);
int BoardDramProbe(UINT64 *Base, UINT64 *Size);
int BoardFbSetup(BOOT_HANDOFF *Info);

int BoardBringUpIdentityMapEnable(void);
/* Pixel DECON 专用；Handoff 共用，MIX2 空实现 */
int BoardBringUpDeconTriggerEnable(void);
/* BRINGUP：在已映射的 splash 上画字 / 彩条 */
int BoardBringUpSplashClear(void);
int BoardBringUpSplashPaint(const char *Text);
int BoardBringUpSplashPaintLine(UINT32 Top, const char *Text);
/* PR-MIX2-USB-1：DWC3 MMIO 可读并切 Host */
int BoardMix2UsbHostProbe(void);
/* PR-MIX2-USB-2：xHCI 起环后热插扫坞 hub（不返回） */
void BoardMix2UsbHubWatch(void);
/* PR-PHONE-PWR-0：PON 短按 → fastboot（HubWatch 内轮询；勿依赖 Tasks） */
int BoardPowerButtonPressed(void);

#endif /* BOARD_H */
