/*
 * Board.h — Raspberry Pi 3B+ 板包门面
 */
#ifndef BOARD_H
#define BOARD_H

#include "BoardConfig.h"
#include "BootTypes.h"
#include "BootHandoff.h"

const char *BoardName(void);

/*
 * BoardDramProbe — 固件 mailbox GET_ARM_MEMORY（BCM tag 0x00010005）。
 * 成功 0 并填 Base/Size；失败 -1（不猜 DRAM）。
 */
int BoardDramProbe(UINT64 *Base, UINT64 *Size);

/* BoardFbSetup — mailbox 分配 FB 并填 Handoff；成功 0 */
int BoardFbSetup(BOOT_HANDOFF *Info);

#endif /* BOARD_H */
