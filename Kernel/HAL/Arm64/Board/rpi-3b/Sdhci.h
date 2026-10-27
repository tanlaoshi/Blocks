/*
 * Sdhci.h — BCM2835 Arasan SDHCI @ 0x3F300000（Pi3B WiFi SDIO）
 *
 * U-Boot bcm2837-rpi-3-b.dts：&sdhci → emmc_gpio34（无线）；
 * µSD 是 SDHOST@0x3F202000（见 Sdhost.h）。本头仅归档/对照。
 */
#ifndef BOARD_SDHCI_H
#define BOARD_SDHCI_H

#include "BootTypes.h"

#define SDHCI_BASE           0x3F300000ULL

#define SDHCI_BLOCK_SIZE     0x04
#define SDHCI_BLOCK_COUNT    0x06
#define SDHCI_ARGUMENT       0x08
#define SDHCI_TRANSFER_MODE  0x0C
#define SDHCI_COMMAND        0x0E
#define SDHCI_RESPONSE       0x10
#define SDHCI_BUFFER         0x20
#define SDHCI_PRESENT_STATE  0x24
#define SDHCI_HOST_CONTROL   0x28
#define SDHCI_POWER_CONTROL  0x29
#define SDHCI_CLOCK_CONTROL  0x2C
#define SDHCI_TIMEOUT_CONTROL 0x2E
#define SDHCI_SOFTWARE_RESET 0x2F
#define SDHCI_INT_STATUS     0x30
#define SDHCI_INT_ENABLE     0x34
#define SDHCI_SIGNAL_ENABLE  0x38
#define SDHCI_CAPABILITIES   0x40
#define SDHCI_HOST_VERSION   0xFE
#define SDHCI_SPEC_300       2u
#define SDHCI_DIV_MASK       0xFFu
#define SDHCI_DIV_MASK_LEN   8u
#define SDHCI_DIV_HI_MASK    0x300u
#define SDHCI_DIVIDER_SHIFT  8u
#define SDHCI_DIVIDER_HI_SHIFT 6u
#define SDHCI_MAX_DIV_200    256u
#define SDHCI_MAX_DIV_300    2046u

#define SDHCI_CMD_INHIBIT    (1u << 0)
#define SDHCI_DATA_INHIBIT   (1u << 1)
#define SDHCI_DATA_AVAILABLE (1u << 11)
#define SDHCI_CARD_PRESENT   (1u << 16)

#define SDHCI_POWER_ON       0x01u
#define SDHCI_POWER_330      0x0Eu

#define SDHCI_CLOCK_INT_EN   (1u << 0)
#define SDHCI_CLOCK_INT_STABLE (1u << 1)
#define SDHCI_CLOCK_CARD_EN  (1u << 2)

#define SDHCI_RESET_ALL      0x01u
#define SDHCI_RESET_CMD      0x02u
#define SDHCI_RESET_DATA     0x04u

#define SDHCI_INT_RESPONSE   (1u << 0)
#define SDHCI_INT_DATA_END   (1u << 1)
#define SDHCI_INT_DATA_AVAIL (1u << 5)
#define SDHCI_INT_ERROR      (1u << 15)

#define SDHCI_TRNS_BLK_CNT_EN (1u << 1)
#define SDHCI_TRNS_READ      (1u << 4)

#define SDHCI_CMD_RESP_NONE  0x00u
#define SDHCI_CMD_RESP_LONG  0x01u
#define SDHCI_CMD_RESP_SHORT 0x02u
#define SDHCI_CMD_RESP_SHORT_BUSY 0x03u
#define SDHCI_CMD_CRC        0x08u
#define SDHCI_CMD_INDEX      0x10u
#define SDHCI_CMD_DATA       0x20u

#define SDHCI_MAKE_CMD(C, F) ((((UINT32)(C) & 0xFFu) << 8) | ((UINT32)(F) & 0xFFu))

void BoardSdhciRegister(void);

#endif
