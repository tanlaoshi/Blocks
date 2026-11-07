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
/* BRINGUP：在已映射的 splash 上画字 / 彩条 */
int BoardBringUpSplashPaint(const char *Text);

#endif /* BOARD_H */
