/*
 * UsbHost/QusbPhy.c — MSM8998 QUSB2：线刷 DT init-seq + Linux v2 power-on
 *
 * 证据：chiron DT `qcom,qusb2phy-v2` @0xC012000；`qcom,qusb-phy-init-seq`；
 * 对表 torvalds phy-qcom-qusb2.c（msm8998 regs / CORE_READY）。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_MIX2_USB_HOST_BRINGUP
#define BOARD_MIX2_USB_HOST_BRINGUP 0
#endif

#if BOARD_MIX2_USB_HOST_BRINGUP

#define QUSB_PHY_BASE                 0x0C012000ULL
#define QUSB_TCSR_CLAMP_ADDRESS       0x01FCB24CULL
#define QUSB_TCSR_USB3_DP_PHYMODE     0x01FCB248ULL /* ssphy reg-names tcsr_usb3_dp_phymode */
#define QUSB_PORT_POWERDOWN_OFFSET    0x210u
#define QUSB_PORT_UTMI_STATUS_OFFSET  0x0F4u
#define QUSB_PLL_STATUS_OFFSET        0x1A0u
#define QUSB_PLL_CORE_INPUT_OVERRIDE  0x0A8u
#define QUSB_POWER_DOWN_BIT           (1u << 0)
#define QUSB_CORE_READY_STATUS_BIT    (1u << 0)
#define QUSB_PLL_CORE_OVERRIDE_CORE_READY_EN (1u << 0)
#define QUSB_PLL_CORE_OVERRIDE_CORE_RESET    (1u << 1)

/*
 * DT `qcom,qusb-phy-init-seq`：成对 <value offset>（字节写）。
 * 0x80@0, 0x13@4, 0x7c@0x18c, 0x80@0x2c, 0xa@0x184, 0xf0@0x23c, 0xf@0x240
 */
static const UINT16 sInitSequence[] = {
    0x0080u, 0x0000u, 0x0013u, 0x0004u, 0x007cu, 0x018cu, 0x0080u, 0x002cu,
    0x000au, 0x0184u, 0x00f0u, 0x023cu, 0x000fu, 0x0240u,
};

static volatile UINT8 *QusbByte(UINT32 Offset) {
    return (volatile UINT8 *)(UINTN)(QUSB_PHY_BASE + (UINT64)Offset);
}

static void QusbWriteByte(UINT32 Offset, UINT8 Value) {
    *QusbByte(Offset) = Value;
}

static UINT8 QusbReadByte(UINT32 Offset) {
    return *QusbByte(Offset);
}

static void QusbSetBits(UINT32 Offset, UINT8 Bits) {
    QusbWriteByte(Offset, (UINT8)(QusbReadByte(Offset) | Bits));
}

static void QusbClearBits(UINT32 Offset, UINT8 Bits) {
    QusbWriteByte(Offset, (UINT8)(QusbReadByte(Offset) & (UINT8)(~Bits)));
}

UINT8 UsbHostQusbLineStateRead(void) {
    return QusbReadByte(QUSB_PORT_UTMI_STATUS_OFFSET);
}

int UsbHostQusbPhyInitialize(void) {
    UINTN PairIndex;
    UINT8 Status;
    UINTN Guard;
    char Line[40];
    char Hex[12];

    /* 释放 dig clamp（第二 reg tcsr_clamp_dig_n_1p8）；公开惯例写 0 */
    UsbHostWriteMmio32(QUSB_TCSR_CLAMP_ADDRESS, 0);
    /* 选 USB3 而非 DP（phy-msm-ssusb-qmp：writel 0） */
    UsbHostWriteMmio32(QUSB_TCSR_USB3_DP_PHYMODE, 0);

    /* 先置 POWER_DOWN，再灌 DT 表，再清 POWER_DOWN（Linux qusb2_phy_init） */
    QusbSetBits(QUSB_PORT_POWERDOWN_OFFSET, QUSB_POWER_DOWN_BIT);

    for (PairIndex = 0; PairIndex + 1u < sizeof(sInitSequence) / sizeof(sInitSequence[0]);
         PairIndex += 2u) {
        UINT8 Value = (UINT8)(sInitSequence[PairIndex] & 0xFFu);
        UINT32 Offset = (UINT32)sInitSequence[PairIndex + 1u];
        QusbWriteByte(Offset, Value);
    }

    QusbClearBits(QUSB_PORT_POWERDOWN_OFFSET, QUSB_POWER_DOWN_BIT);
    UsbHostStallYield(200000u);

    /* msm8998/v2：has_pll_override → 脉冲 CORE_RESET 再等 CORE_READY */
    QusbSetBits(QUSB_PLL_CORE_INPUT_OVERRIDE,
                QUSB_PLL_CORE_OVERRIDE_CORE_READY_EN | QUSB_PLL_CORE_OVERRIDE_CORE_RESET);
    UsbHostStallYield(50000u);
    QusbClearBits(QUSB_PLL_CORE_INPUT_OVERRIDE, QUSB_PLL_CORE_OVERRIDE_CORE_RESET);
    UsbHostStallYield(200000u);

    Status = QusbReadByte(QUSB_PLL_STATUS_OFFSET);
    UsbHostFormatHexadecimal32(Hex, Status);
    UsbHostCopyText(Line, "phy ", sizeof(Line));
    UsbHostAppendText(Line, Hex, sizeof(Line));
    BoardBringUpSplashPaintLine(208, Line);

    if ((Status & QUSB_CORE_READY_STATUS_BIT) == 0) {
        BoardBringUpSplashPaintLine(256, "phy unlocked");
        /* 仍继续：aboot 可能已锁过；让根口继续扫 */
        for (Guard = 0; Guard < 10u; Guard++) {
            UsbHostStallYield(100000u);
            Status = QusbReadByte(QUSB_PLL_STATUS_OFFSET);
            if ((Status & QUSB_CORE_READY_STATUS_BIT) != 0) {
                break;
            }
        }
    }

    if ((Status & QUSB_CORE_READY_STATUS_BIT) != 0) {
        BoardBringUpSplashPaintLine(208, "phy ready");
        return 0;
    }
    return -1;
}

#else

UINT8 UsbHostQusbLineStateRead(void) {
    return 0;
}

int UsbHostQusbPhyInitialize(void) {
    return -1;
}

#endif
