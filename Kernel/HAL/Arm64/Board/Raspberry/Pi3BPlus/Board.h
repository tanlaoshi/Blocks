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

/* SD 电源域 + 时钟（mbox；对齐 U-Boot bcm2835_get_mmc_clock） */
int BoardSdhciPowerOn(void);
int BoardSdhciPowerOff(void);
int BoardMboxGetClockHz(UINT32 ClockId, UINT32 *OutHz);

#define BOARD_MBOX_CLOCK_EMMC 1u
#define BOARD_MBOX_CLOCK_CORE 4u /* SDHOST 分频母钟（bcm2835_sdhost） */
#define BOARD_MBOX_POWER_SDHCI 0u

#endif /* BOARD_H */
