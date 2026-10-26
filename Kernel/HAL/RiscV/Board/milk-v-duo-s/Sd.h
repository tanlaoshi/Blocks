/*
 * Sd.h — Milk-V Duo S µSD：DWC MSHC（SDHCI）@ 0x04310000
 *
 * 上游 cv18xx.dtsi：mmc@4310000 compatible "sophgo,cv1800b-dwcmshc"（sdhci0）。
 * 寄存器按 SD Host Controller 简化规格；厂商区按 Linux sdhci-of-dwcmshc.c
 * cv18xx_sdhci_reset（P_VENDOR_AREA1 @0xE8）。
 * eMMC 不在本文件（勿把 SD 槽写成 eMMC）。
 */
#ifndef BOARD_SD_H
#define BOARD_SD_H

#include "BootTypes.h"

#define SDHCI_BASE 0x04310000ULL

#define SDHCI_BLOCK_SIZE      0x04
#define SDHCI_BLOCK_COUNT     0x06
#define SDHCI_ARGUMENT        0x08
#define SDHCI_TRANSFER_MODE   0x0C
#define SDHCI_COMMAND         0x0E
#define SDHCI_RESPONSE        0x10
#define SDHCI_BUFFER          0x20
#define SDHCI_PRESENT_STATE   0x24
#define SDHCI_HOST_CONTROL    0x28
#define SDHCI_POWER_CONTROL   0x29
#define SDHCI_CLOCK_CONTROL   0x2C
#define SDHCI_TIMEOUT_CONTROL 0x2E
#define SDHCI_SOFTWARE_RESET  0x2F
#define SDHCI_INT_STATUS      0x30
#define SDHCI_INT_ENABLE      0x34
#define SDHCI_SIGNAL_ENABLE   0x38
#define SDHCI_CAPABILITIES    0x40
#define SDHCI_HOST_CONTROL2   0x3E
#define SDHCI_P_VENDOR_AREA1  0xE8
#define SDHCI_HOST_VERSION    0xFE

#define SDHCI_SPEC_300     2u
#define SDHCI_DIV_MASK     0xFFu
#define SDHCI_DIV_MASK_LEN 8u
#define SDHCI_DIV_HI_MASK  0x300u
#define SDHCI_DIVIDER_SHIFT    8u
#define SDHCI_DIVIDER_HI_SHIFT 6u
#define SDHCI_MAX_DIV_300  2046u

#define SDHCI_CMD_INHIBIT    (1u << 0)
#define SDHCI_DATA_INHIBIT   (1u << 1)
#define SDHCI_CARD_INSERTED  (1u << 16)
#define SDHCI_CARD_STABLE    (1u << 17)
#define SDHCI_WIDTH_4BIT     (1u << 1)

#define SDHCI_POWER_ON  0x01u
#define SDHCI_POWER_330 0x0Eu

#define SDHCI_CLOCK_INT_EN     (1u << 0)
#define SDHCI_CLOCK_INT_STABLE (1u << 1)
#define SDHCI_CLOCK_CARD_EN    (1u << 2)

#define SDHCI_RESET_ALL  0x01u
#define SDHCI_RESET_CMD  0x02u
#define SDHCI_RESET_DATA 0x04u

#define SDHCI_INT_RESPONSE (1u << 0)
#define SDHCI_INT_DATA_END (1u << 1)
#define SDHCI_INT_DATA_AVAIL (1u << 5)
#define SDHCI_INT_ERROR    (1u << 15)

#define SDHCI_TRNS_BLK_CNT_EN (1u << 1)
#define SDHCI_TRNS_READ       (1u << 4)

#define SDHCI_CMD_RESP_NONE       0x00u
#define SDHCI_CMD_RESP_LONG       0x01u
#define SDHCI_CMD_RESP_SHORT      0x02u
#define SDHCI_CMD_RESP_SHORT_BUSY 0x03u
#define SDHCI_CMD_CRC   0x08u
#define SDHCI_CMD_INDEX 0x10u
#define SDHCI_CMD_DATA  0x20u

#define SDHCI_MAKE_CMD(C, F) ((((UINT32)(C) & 0xFFu) << 8) | ((UINT32)(F) & 0xFFu))

/* cv18xx 厂商区偏移（相对 P_VENDOR_AREA1，不是绝对地址） */
#define CV_MSHC_CTRL   0x00u
#define CV_PHY_TX_RX   0x40u
#define CV_PHY_CONFIG  0x4Cu
#define CV_LATENCY_1T  (1u << 1)
#define CV_PHY_TX_BPS  (1u << 0)
/* TX src = invert clk（bits 9:8 = 1），RX src = invert（bits 25:24 = 1），dly = 0 */
#define CV_PHY_DLY_BYPASS ((1u << 8) | (1u << 24))

void BoardSdRegister(void);

#endif
