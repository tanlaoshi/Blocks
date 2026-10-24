/*
 * Board.h — QEMU aarch64 virt 板包门面（PR-B2）
 * B3 起可扩 BoardFillBootHandoff / 兼容表；不进 Common。
 */
#ifndef BOARD_H
#define BOARD_H

#include "BoardConfig.h"
#include "BootTypes.h"
#include "BootHandoff.h"

const char *BoardName(void);

/* virt：无 VC mailbox；DRAM/FB 只信 DTB/ramfb。恒失败。 */
int BoardDramProbe(UINT64 *Base, UINT64 *Size);

int BoardFbSetup(BOOT_HANDOFF *Info);

#endif /* BOARD_H */
