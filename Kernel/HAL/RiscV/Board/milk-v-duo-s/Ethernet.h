/*
 * Ethernet.h — Milk-V Duo S / CV181x DesignWare MAC（net-1）+ 板载 EPHY（net-2）
 *
 * net-1：VERSION @0x20（dwmac-3.70a）；时钟 / 释放 RST_ETH0。
 * net-2：GMAC MII @0x10/0x14；扫 PHY；BMSR.Link；无 link 不称 Ready。
 */
#ifndef BOARD_ETHERNET_H
#define BOARD_ETHERNET_H

#include "BootTypes.h"

#define DUO_ETHERNET_BASE                 0x04070000ULL

#define DUO_CLKGEN_BASE                0x03002000ULL
#define DUO_CLK_ENABLE_0               0x000u
#define DUO_CLK_ENABLE_500M_ETH0       (1u << 25)
#define DUO_CLK_ENABLE_AXI4_ETH0       (1u << 26)
#define DUO_CLK_DIVIDER_500M_ETH0      0x08Cu
#define DUO_CLK_BYPASS_0               0x030u

#define DUO_ETHERNET_TOP_SOFT_RESET       0x03003000ULL
#define DUO_ETHERNET_SOFT_RESET_BIT       (1u << 12)

#define DUO_ETHERNET_VERSION              0x020u
#define DUO_ETHERNET_GMAC4_VERSION        0x110u
#define DUO_ETHERNET_MAC_CONFIG           0x000u
#define DUO_ETHERNET_FRAME_FILTER         0x004u
#define DUO_ETHERNET_ADDRESS0_HIGH        0x040u
#define DUO_ETHERNET_ADDRESS0_LOW         0x044u

/* GMAC Configuration（dwmac1000.h） */
#define DUO_ETHERNET_MAC_TRANSMITTER_ENABLE   (1u << 3)
#define DUO_ETHERNET_MAC_RECEIVER_ENABLE      (1u << 2)
#define DUO_ETHERNET_MAC_DUPLEX               (1u << 11)
#define DUO_ETHERNET_MAC_LOOPBACK             (1u << 12)
#define DUO_ETHERNET_MAC_SPEED_100            (1u << 14)
#define DUO_ETHERNET_MAC_PORT_SELECT_MII      (1u << 15)
#define DUO_ETHERNET_FRAME_FILTER_PROMISCUOUS (1u << 0)
#define DUO_ETHERNET_ADDRESS_ENABLE           (1u << 31)

/* DMA CSR（dwmac_dma.h；相对 MAC 基址） */
#define DUO_ETHERNET_DMA_BUS_MODE             0x1000u
#define DUO_ETHERNET_DMA_TRANSMIT_POLL        0x1004u
#define DUO_ETHERNET_DMA_RECEIVE_POLL         0x1008u
#define DUO_ETHERNET_DMA_RECEIVE_LIST_BASE    0x100Cu
#define DUO_ETHERNET_DMA_TRANSMIT_LIST_BASE   0x1010u
#define DUO_ETHERNET_DMA_STATUS               0x1014u
#define DUO_ETHERNET_DMA_OPERATION_MODE       0x1018u
#define DUO_ETHERNET_DMA_INTERRUPT_ENABLE     0x101Cu
#define DUO_ETHERNET_DMA_HARDWARE_FEATURE     0x1058u
#define DUO_ETHERNET_DMA_SOFTWARE_RESET       (1u << 0)
#define DUO_ETHERNET_DMA_PBL_SHIFT            8u
#define DUO_ETHERNET_DMA_START_TRANSMIT       (1u << 13)
#define DUO_ETHERNET_DMA_START_RECEIVE        (1u << 1)
#define DUO_ETHERNET_DMA_TRANSMIT_STORE_FORWARD (1u << 21)
#define DUO_ETHERNET_DMA_RECEIVE_STORE_FORWARD  (1u << 25)
#define DUO_ETHERNET_DMA_OPERATE_SECOND_FRAME (1u << 2)
#define DUO_ETHERNET_DMA_FEATURE_ENHANCED_DESCRIPTOR (1u << 24)

/*
 * Normal 描述符位（descs.h）。本片 dmafeat ENHDESSEL=0（真机 0x420d5925）。
 * TX：OWN@TDES0；FS/LS/IC/ER/size@TDES1。RX：OWN/len@RDES0；size/ER@RDES1。
 */
#define DUO_ETHERNET_DESCRIPTOR_OWN                    (1u << 31)
#define DUO_ETHERNET_RECEIVE_DESCRIPTOR_FRAME_LENGTH_SHIFT 16u
#define DUO_ETHERNET_RECEIVE_DESCRIPTOR_FRAME_LENGTH_MASK  0x3FFFu
#define DUO_ETHERNET_RECEIVE_DESCRIPTOR_ERROR_SUMMARY  (1u << 15)
#define DUO_ETHERNET_TRANSMIT_BUFFER1_SIZE_MASK        0x7FFu
#define DUO_ETHERNET_TRANSMIT_END_RING                 (1u << 25)
#define DUO_ETHERNET_TRANSMIT_FIRST_SEGMENT            (1u << 29)
#define DUO_ETHERNET_TRANSMIT_LAST_SEGMENT             (1u << 30)
#define DUO_ETHERNET_TRANSMIT_INTERRUPT                (1u << 31)
#define DUO_ETHERNET_RECEIVE_BUFFER1_SIZE_MASK         0x7FFu
#define DUO_ETHERNET_RECEIVE_END_RING                  (1u << 25)

#define DUO_ETHERNET_DMA_RING_COUNT   4u
#define DUO_ETHERNET_DMA_BUFFER_SIZE  1536u

/* dwmac1000：MII Address / Data（协议名 MII 保留） */
#define DUO_ETHERNET_MII_ADDRESS          0x010u
#define DUO_ETHERNET_MII_DATA             0x014u
#define DUO_ETHERNET_MII_BUSY             (1u << 0)
#define DUO_ETHERNET_MII_WRITE            (1u << 1)
#define DUO_ETHERNET_MII_PHY_SHIFT        11u
#define DUO_ETHERNET_MII_REGISTER_SHIFT   6u
#define DUO_ETHERNET_MII_CLOCK_RANGE_SHIFT 2u
/* DTB eth_csrclk=250MHz → STMMAC_CSR_150_250M */
#define DUO_ETHERNET_MII_CLOCK_RANGE      0x4u

#define DUO_EPHY_BASE                  0x03009000ULL
#define DUO_EPHY_APB_SELECT            0x03009804ULL
#define DUO_EPHY_CONTROL               0x03009800ULL
/* APB：每 MII 寄存器占 4 字节（U-Boot cv181x_ephy_id_init） */
#define DUO_EPHY_APB(Register) \
    (DUO_EPHY_BASE + ((UINT64)(Register) * 4u))
#define DUO_EPHY_PAGE_SELECT           DUO_EPHY_APB(0x1Fu)
#define DUO_EPHY_PAGE5_POWER           DUO_EPHY_APB(0x10u)
#define DUO_EPHY_APB_IDENTIFIER_1      DUO_EPHY_APB(2u)
#define DUO_EPHY_APB_IDENTIFIER_2      DUO_EPHY_APB(3u)
/* 硅片默认 ID=0；须 APB 写入后 MDIO 才能扫到（同 U-Boot） */
#define DUO_EPHY_IDENTIFIER_1_VALUE    0x0043u
#define DUO_EPHY_IDENTIFIER_2_VALUE    0x5649u
/* TOP wrap：Linux cvitek「EPHY start auto-neg」 */
#define DUO_EPHY_CONTROL_AUTONEG_START 0x090eu

/* IEEE 802.3 clause 22 寄存器号 / BMCR·BMSR 位 */
#define DUO_PHY_BASIC_CONTROL          0u
#define DUO_PHY_BASIC_STATUS           1u
#define DUO_PHY_IDENTIFIER_1           2u
#define DUO_PHY_IDENTIFIER_2           3u
#define DUO_PHY_AUTONEG_ADVERTISE      4u
#define DUO_PHY_LINK_PARTNER_ABILITY   5u
#define DUO_PHY_BASIC_CONTROL_FULL_DUPLEX       (1u << 8)
#define DUO_PHY_BASIC_CONTROL_AUTONEG_RESTART   (1u << 9)
#define DUO_PHY_BASIC_CONTROL_ISOLATE           (1u << 10)
#define DUO_PHY_BASIC_CONTROL_RESET             (1u << 15)
#define DUO_PHY_BASIC_CONTROL_AUTONEG_ENABLE    (1u << 12)
#define DUO_PHY_BASIC_STATUS_LINK               (1u << 2)
#define DUO_PHY_BASIC_STATUS_AUTONEG_COMPLETE   (1u << 5)
/* IEEE Advertise：CSMA + 10/100 H/F（genphy_config_aneg 常用全集） */
#define DUO_PHY_ADVERTISE_CSMA                  0x0001u
#define DUO_PHY_ADVERTISE_10_HALF               0x0020u
#define DUO_PHY_ADVERTISE_10_FULL               0x0040u
#define DUO_PHY_ADVERTISE_100_HALF              0x0080u
#define DUO_PHY_ADVERTISE_100_FULL              0x0100u
#define DUO_PHY_ADVERTISE_DEFAULT               \
    (DUO_PHY_ADVERTISE_CSMA | DUO_PHY_ADVERTISE_10_HALF | \
     DUO_PHY_ADVERTISE_10_FULL | DUO_PHY_ADVERTISE_100_HALF | \
     DUO_PHY_ADVERTISE_100_FULL)

/* MAC + PHY/link + DMA 环回探针；勿 BOARD_HAS_NET */
int BoardEthernetProbe(void);

#endif /* BOARD_ETHERNET_H */
