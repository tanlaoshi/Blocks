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

#endif /* BOARD_H */
