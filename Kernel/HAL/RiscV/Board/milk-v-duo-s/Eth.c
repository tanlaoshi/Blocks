/*
 * Eth.c — Duo S 以太网 MAC 探针（net-1）
 *
 * 证据：ver=0 + clken0=ffffffff → 曾误读 DWMAC4@0x110；mainline 为 3.70a@0x20。
 * 另：U-Boot 离场后 MAC 可能仍在 soft-rst；只 OR 释放 RST_ETH0，不做拉低脉冲。
 */
#include "Eth.h"
#include "Board.h"
#include "Hal.h"
#include "SerialLog.h"

static volatile UINT32 *Mmio(UINT64 Addr) {
    return (volatile UINT32 *)(UINTN)Addr;
}

static void Hex32(UINT32 V) {
    char Buf[9];
    HalSerialFormatHex(Buf, V, 8);
    HalSerialBootMark(Buf);
}

static void TagHex(const char *Tag, UINT32 V) {
    HalSerialBootMark(Tag);
    Hex32(V);
}

static void EthSpin(UINT32 N) {
    volatile UINT32 i;
    for (i = 0; i < N; i++) {
        __asm__ volatile("nop");
    }
}

static int VerLooksLive(UINT32 Ver) {
    return Ver != 0u && Ver != 0xFFFFFFFFu;
}

int BoardEthProbe(void) {
    UINT32 Soft;
    UINT32 EnBefore;
    UINT32 EnAfter;
    UINT32 Div;
    UINT32 Byp;
    UINT32 UsbPhy;
    UINT32 Mac0;
    UINT32 Ver20;
    UINT32 Ver110;
    UINT32 Ver;

    HalSerialBootMark("Boot: Duo ETH probe\n");

    /* 对照：USB PHY 同 TOP 窗，应非全 F（Dwc2 已用） */
    UsbPhy = *Mmio(0x03000048ULL);
    Soft = *Mmio(DUO_ETH_TOP_SOFT_RST);
    EnBefore = *Mmio(DUO_CLKGEN_BASE + DUO_CLK_EN_0);
    Div = *Mmio(DUO_CLKGEN_BASE + DUO_CLK_DIV_500M_ETH0);
    Byp = *Mmio(DUO_CLKGEN_BASE + DUO_CLK_BYP_0);

    TagHex("Boot: Duo ETH topusb=", UsbPhy);
    TagHex(" softrst=", Soft);
    TagHex(" clken0=", EnBefore);
    TagHex(" div500=", Div);
    TagHex(" byp0=", Byp);
    HalSerialBootMark("\n");

    /* 释放 RST_ETH0（bit=1 出复位）；勿先清 bit */
    Soft |= DUO_ETH_SOFT_RST_BIT;
    *Mmio(DUO_ETH_TOP_SOFT_RST) = Soft;
    EthSpin(8000u);

    /*
     * 开 eth0 门控。若 EN 读已是全 F，可能是总线假读——仍写 OR，再读 MAC。
     * DIV：Linux 表默认分频域 bit[19:16]；仅当全 0 时写入旁路友好值（bit8 使能类
     * 字段见 clk-cv181x CV181X_CLK 行：div_reg + en_shift）。无证据不猜复杂分频。
     */
    EnAfter = EnBefore | DUO_CLK_EN_500M_ETH0 | DUO_CLK_EN_AXI4_ETH0;
    *Mmio(DUO_CLKGEN_BASE + DUO_CLK_EN_0) = EnAfter;
    /*
     * clk-cv181x：REG_DIV_CLK_500M_ETH0，en 在 bit8，div 在 [19:16]，默认 div=3
     *（500M/3 量级）。BYP bit9=0 走分频。
     */
    if ((Div & 0x000F0000u) == 0u) {
        Div = (3u << 16) | (1u << 8);
        *Mmio(DUO_CLKGEN_BASE + DUO_CLK_DIV_500M_ETH0) = Div;
    }
    Byp &= ~(1u << 9);
    *Mmio(DUO_CLKGEN_BASE + DUO_CLK_BYP_0) = Byp;
    EthSpin(8000u);

    EnAfter = *Mmio(DUO_CLKGEN_BASE + DUO_CLK_EN_0);
    Mac0 = *Mmio(DUO_ETH_BASE + DUO_ETH_MAC_CONFIG);
    Ver20 = *Mmio(DUO_ETH_BASE + DUO_ETH_GMAC_VERSION);
    Ver110 = *Mmio(DUO_ETH_BASE + DUO_ETH_GMAC4_VERSION);

    TagHex("Boot: Duo ETH clken0'=", EnAfter);
    TagHex(" mac0=", Mac0);
    TagHex(" ver20=", Ver20);
    TagHex(" ver110=", Ver110);
    HalSerialBootMark("\n");

    if (VerLooksLive(Ver20)) {
        Ver = Ver20;
        HalSerialBootMark("Boot: Duo ETH id=3.70a@20 ver=");
    } else if (VerLooksLive(Ver110)) {
        Ver = Ver110;
        HalSerialBootMark("Boot: Duo ETH id=EQOS@110 ver=");
    } else {
        HalSerialBootMark("Boot: Duo ETH VERSION dead\n");
        return -1;
    }
    Hex32(Ver);
    HalSerialBootMark(" (no DMA)\n");
    return 0;
}
