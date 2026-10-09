/*
 * Board.h — QEMU aarch64 virt 板包门面
 *
 * 板名等查询；不进 Common（Common 只认 BOOT_INFO / Hal 门面）。
 */
#ifndef BOARD_H
#define BOARD_H

#include "BoardConfig.h"

const char *BoardName(void);

#endif
