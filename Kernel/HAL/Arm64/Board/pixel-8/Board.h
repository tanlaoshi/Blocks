/*
 * Board.h — Pixel 8 板包门面
 */
#ifndef BOARD_H
#define BOARD_H

#include "BoardConfig.h"
#include "BootTypes.h"
#include "BootHandoff.h"

const char *BoardName(void);
int BoardDramProbe(UINT64 *Base, UINT64 *Size);
int BoardFbSetup(BOOT_HANDOFF *Info);

/* BRINGUP：打开最小恒等 MMU；其它板可空实现返回 0 */
int BoardBringUpIdentityMapEnable(void);
/* BRINGUP：重开 DECON0 TRIG_CON（ABL 关触发后屏才刷新） */
int BoardBringUpDeconTriggerEnable(void);
/* BRINGUP：在已映射的 ABL splash 上画字 / 彩条 */
int BoardBringUpSplashClear(void);
int BoardBringUpSplashPaint(const char *Text);
int BoardBringUpSplashPaintLine(UINT32 Top, const char *Text);
/* PR-PIXEL8-USB-1：DWC3 MMIO 可读并切 Host（串口报 id/host） */
int BoardPixel8UsbHostProbe(void);
/* PR-PIXEL8-USB-2：xHCI 起环后热插扫 hub（屏/串口 e/a；不返回） */
void BoardPixel8UsbHubWatch(void);

#endif /* BOARD_H */
