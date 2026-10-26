/*
 * Eth.h — Milk-V Duo S / CV181x MAC 探针（net-1）
 *
 * 对表：
 *   - DTB ethernet@4070000（duo.dtb：cvitek,ethernet；mainline：snps,dwmac-3.70a）
 *   - VERSION：3.70a @ **0x20**（Linux common.h GMAC_VERSION）；亦试 0x110（若 EQOS）
 *   - clkgen @ 0x03002000；EN_0 bit25/26 = eth0 500M + axi4（clk-cv181x.c）
 *   - Soft reset RST_ETH0=12 → TOP SOFT_RST bit12（仅释放，不拉低脉冲）
 *
 * 本刀：clk + 释放复位 + 读 VERSION。禁止 DMA / PHY / HalNet。
 */
#ifndef BOARD_DUO_ETH_H
#define BOARD_DUO_ETH_H

#include "BootTypes.h"

#define DUO_ETH_BASE              0x04070000ULL

#define DUO_CLKGEN_BASE           0x03002000ULL
#define DUO_CLK_EN_0              0x000u
#define DUO_CLK_EN_500M_ETH0      (1u << 25)
#define DUO_CLK_EN_AXI4_ETH0      (1u << 26)
#define DUO_CLK_DIV_500M_ETH0     0x08Cu
#define DUO_CLK_BYP_0             0x030u

/* TOP soft reset（与 Dwc2 USB 同寄存器）；RST_ETH0 = 12 */
#define DUO_ETH_TOP_SOFT_RST      0x03003000ULL
#define DUO_ETH_SOFT_RST_BIT      (1u << 12)

/* Synopsys：3.70a → 0x20；GMAC4/EQOS → 0x110 */
#define DUO_ETH_GMAC_VERSION      0x020u
#define DUO_ETH_GMAC4_VERSION     0x110u
#define DUO_ETH_MAC_CONFIG        0x000u

int BoardEthProbe(void);

#endif /* BOARD_DUO_ETH_H */
