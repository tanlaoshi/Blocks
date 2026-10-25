/*
 * Sdhost.h — BCM2835 SDHOST @ 0x3F202000（Pi3B+ µSD）
 *
 * U-Boot bcm2837-rpi-3-b-plus.dts：&sdhost → sdhost_gpio48 = 卡；
 * Arasan SDHCI@0x3F300000 → emmc_gpio34 = WiFi，勿当 µSD。
 * 寄存器布局对齐 U-Boot drivers/mmc/bcm2835_sdhost.c。
 */
#ifndef BOARD_SDHOST_H
#define BOARD_SDHOST_H

#include "BootTypes.h"

#define SDHOST_BASE 0x3F202000ULL

#define SDCMD  0x00
#define SDARG  0x04
#define SDTOUT 0x08
#define SDCDIV 0x0c
#define SDRSP0 0x10
#define SDRSP1 0x14
#define SDRSP2 0x18
#define SDRSP3 0x1c
#define SDHSTS 0x20
#define SDVDD  0x30
#define SDEDM  0x34
#define SDHCFG 0x38
#define SDHBCT 0x3c
#define SDDATA 0x40
#define SDHBLC 0x50

#define SDCMD_NEW_FLAG       0x8000u
#define SDCMD_FAIL_FLAG      0x4000u
#define SDCMD_BUSYWAIT       0x800u
#define SDCMD_NO_RESPONSE    0x400u
#define SDCMD_LONG_RESPONSE  0x200u
#define SDCMD_WRITE_CMD      0x80u
#define SDCMD_READ_CMD       0x40u
#define SDCMD_CMD_MASK       0x3fu

#define SDCDIV_MAX_CDIV      0x7ffu

#define SDHSTS_BUSY_IRPT     0x400u
#define SDHSTS_BLOCK_IRPT    0x200u
#define SDHSTS_REW_TIME_OUT  0x80u
#define SDHSTS_CMD_TIME_OUT  0x40u
#define SDHSTS_CRC16_ERROR   0x20u
#define SDHSTS_CRC7_ERROR    0x10u
#define SDHSTS_FIFO_ERROR    0x08u

#define SDHSTS_CLEAR_MASK                                                      \
    (SDHSTS_BUSY_IRPT | SDHSTS_BLOCK_IRPT | SDHSTS_REW_TIME_OUT |              \
     SDHSTS_CMD_TIME_OUT | SDHSTS_CRC16_ERROR | SDHSTS_CRC7_ERROR |            \
     SDHSTS_FIFO_ERROR)

#define SDHSTS_ERROR_MASK                                                      \
    (SDHSTS_CMD_TIME_OUT | SDHSTS_CRC16_ERROR | SDHSTS_CRC7_ERROR |            \
     SDHSTS_REW_TIME_OUT | SDHSTS_FIFO_ERROR)

#define SDHCFG_BUSY_IRPT_EN  (1u << 10)
#define SDHCFG_SLOW_CARD     (1u << 3)
#define SDHCFG_WIDE_EXT_BUS  (1u << 2)
#define SDHCFG_WIDE_INT_BUS  (1u << 1)

#define SDVDD_POWER_OFF 0u
#define SDVDD_POWER_ON  1u

#define SDEDM_FORCE_DATA_MODE (1u << 19)
#define SDEDM_FIFO_FILL_SHIFT 4u
#define SDEDM_FIFO_FILL_MASK  0x1fu
#define SDEDM_WRITE_THRESHOLD_SHIFT 9u
#define SDEDM_READ_THRESHOLD_SHIFT  14u
#define SDEDM_THRESHOLD_MASK        0x1fu
#define SDEDM_FSM_MASK        0xfu
#define SDEDM_FSM_IDENTMODE   0x0u
#define SDEDM_FSM_DATAMODE    0x1u
#define SDEDM_FSM_READDATA    0x2u
#define SDEDM_FSM_READWAIT    0x4u
#define SDEDM_FSM_READCRC     0x5u
#define SDEDM_FSM_WRITESTART1 0xau

#define FIFO_READ_THRESHOLD  4u
#define FIFO_WRITE_THRESHOLD 4u
#define SDDATA_FIFO_WORDS    16u
#define SDDATA_FIFO_PIO_BURST 8u

void BoardSdhostRegister(void);

#endif
