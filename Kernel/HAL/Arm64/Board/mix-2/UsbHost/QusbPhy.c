/*
 * UsbHost/QusbPhy.c — MSM8998 QUSB2：线刷 DT init-seq + phy-msm-qusb-v2
 *
 * 证据：chiron DT `qcom,qusb2phy-v2` @0xC012000；`qcom,qusb-phy-init-seq`。
 * 对表 phy-msm-qusb-v2：`tcsr_clamp_dig_n_1p8` 连线写 1 解钳、写 0 钳住漏电；
 * 真机 `a2` 仍 `ls80`/`p*:200` → 先前误写 0，D+/D- 数字域被钳。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_MIX2_USB_HOST_BRINGUP
#define BOARD_MIX2_USB_HOST_BRINGUP 0
#endif

#if BOARD_MIX2_USB_HOST_BRINGUP

#define QUSB_PHY_BASE                 0x0C012000ULL
#define QUSB_TCSR_CLAMP_ADDRESS       0x01FCB24CULL
#define QUSB_TCSR_USB3_DP_PHYMODE     0x01FCB248ULL
#define QUSB_PORT_POWERDOWN_OFFSET    0x210u
#define QUSB_PORT_UTMI_STATUS_OFFSET  0x0F4u /* 非 v2 线态；刷屏对照用 */
#define QUSB_PLL_STATUS_OFFSET        0x1A0u
#define QUSB_POWER_DOWN_BIT           (1u << 0)
#define QUSB_CORE_READY_STATUS_BIT    (1u << 0)
#define QUSB_TCSR_CLAMP_DEASSERT      1u /* _n：1=解钳（连线），0=钳住 */

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

static void QusbWriteInitSequence(void) {
    UINTN PairIndex;

    for (PairIndex = 0; PairIndex + 1u < sizeof(sInitSequence) / sizeof(sInitSequence[0]);
         PairIndex += 2u) {
        UINT8 Value = (UINT8)(sInitSequence[PairIndex] & 0xFFu);
        UINT32 Offset = (UINT32)sInitSequence[PairIndex + 1u];
        QusbWriteByte(Offset, Value);
    }
}

static int QusbWaitCoreReady(void) {
    UINT8 Status;
    UINTN Guard;

    UsbHostStallYield(200000u);
    Status = QusbReadByte(QUSB_PLL_STATUS_OFFSET);
    if ((Status & QUSB_CORE_READY_STATUS_BIT) != 0) {
        return 0;
    }
    for (Guard = 0; Guard < 10u; Guard++) {
        UsbHostStallYield(100000u);
        Status = QusbReadByte(QUSB_PLL_STATUS_OFFSET);
        if ((Status & QUSB_CORE_READY_STATUS_BIT) != 0) {
            return 0;
        }
    }
    return -1;
}

UINT8 UsbHostQusbLineStateRead(void) {
    return QusbReadByte(QUSB_PORT_UTMI_STATUS_OFFSET);
}

UINT8 UsbHostQusbClampByte(void) {
    return (UINT8)(UsbHostReadMmio32(QUSB_TCSR_CLAMP_ADDRESS) & 0xFFu);
}

static int QusbPowerOnWithSequence(void) {
    /* 先解钳，再松 POWER_DOWN（phy-msm-qusb-v2 连线路径） */
    UsbHostWriteMmio32(QUSB_TCSR_CLAMP_ADDRESS, QUSB_TCSR_CLAMP_DEASSERT);
    UsbHostWriteMmio32(QUSB_TCSR_USB3_DP_PHYMODE, 0);

    QusbSetBits(QUSB_PORT_POWERDOWN_OFFSET, QUSB_POWER_DOWN_BIT);
    QusbWriteInitSequence();
    QusbClearBits(QUSB_PORT_POWERDOWN_OFFSET, QUSB_POWER_DOWN_BIT);
    return QusbWaitCoreReady();
}

int UsbHostQusbPhyInitialize(void) {
    /* 勿 PaintLine：hub 监视清屏后仍会被 AttachKick 路径叠到状态行 */
    return QusbPowerOnWithSequence();
}

/*
 * CC attach 后轻踢：只保解钳 + 清 GUSB2 suspend。
 * 禁止再 POWER_DOWN 整表重灌——会抖断已 CCS 的 hub，根口复位见 e10。
 */
int UsbHostQusbPhyAttachKick(void) {
    UINT32 Usb2PhyConfiguration;

    UsbHostWriteMmio32(QUSB_TCSR_CLAMP_ADDRESS, QUSB_TCSR_CLAMP_DEASSERT);
    Usb2PhyConfiguration =
        UsbHostReadMmio32(DWC3_REGISTER_BASE + DWC3_GUSB2PHYCFG_OFFSET);
    Usb2PhyConfiguration &= ~((1u << 6) | DWC3_PHY_SOFTWARE_RESET);
    UsbHostWriteMmio32(DWC3_REGISTER_BASE + DWC3_GUSB2PHYCFG_OFFSET,
                       Usb2PhyConfiguration);
    UsbHostStallYield(50000u);
    return 0;
}

#else

UINT8 UsbHostQusbLineStateRead(void) {
    return 0;
}

UINT8 UsbHostQusbClampByte(void) {
    return 0;
}

int UsbHostQusbPhyInitialize(void) {
    return -1;
}

int UsbHostQusbPhyAttachKick(void) {
    return -1;
}

#endif
