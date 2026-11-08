/*
 * UsbHost/Qscratch.c — ssusb 第二段 QSCRATCH @0xC016000
 *
 * 证据：chiron DT `ssusb@a800000` reg[1]=0xc016000；`dwc3` maximum-speed=high-speed；
 * `ssphy` 有 `qcom,vbus-valid-override`。
 * 对表 torvalds `dwc3-qcom.c`：select_utmi_clk + host 侧 VBUS/SESSVLD 软件置位
 * （坞给手机供电时 SoC 不驱动 VBUS，须 override 才让 UTMI 认线）。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_MIX2_USB_HOST_BRINGUP
#define BOARD_MIX2_USB_HOST_BRINGUP 0
#endif

#if BOARD_MIX2_USB_HOST_BRINGUP

#define QSCRATCH_REGISTER_BASE           0x0C016000ULL
#define QSCRATCH_GENERAL_CFG_OFFSET      0x08u
#define QSCRATCH_HS_PHY_CTRL_OFFSET      0x10u
#define QSCRATCH_SS_PHY_CTRL_OFFSET      0x30u

#define PIPE_UTMI_CLOCK_SELECT           (1u << 0)
#define PIPE3_PHY_STATUS_SOFTWARE        (1u << 3)
#define PIPE_UTMI_CLOCK_DISABLE          (1u << 8)
#define UTMI_OTG_VBUS_VALID              (1u << 20)
#define SOFTWARE_SESSION_VALID_SELECT    (1u << 28)
#define LANE0_POWER_PRESENT              (1u << 24)

static void QscratchSetBits(UINT32 Offset, UINT32 Bits) {
    UINT64 Address = QSCRATCH_REGISTER_BASE + (UINT64)Offset;
    UINT32 Value = UsbHostReadMmio32(Address);
    UsbHostWriteMmio32(Address, Value | Bits);
    (void)UsbHostReadMmio32(Address);
}

static void QscratchClearBits(UINT32 Offset, UINT32 Bits) {
    UINT64 Address = QSCRATCH_REGISTER_BASE + (UINT64)Offset;
    UINT32 Value = UsbHostReadMmio32(Address);
    UsbHostWriteMmio32(Address, Value & ~Bits);
    (void)UsbHostReadMmio32(Address);
}

static void QscratchSelectUtmiAsPipeClock(void) {
    QscratchSetBits(QSCRATCH_GENERAL_CFG_OFFSET, PIPE_UTMI_CLOCK_DISABLE);
    UsbHostStallYield(200000u);
    QscratchSetBits(QSCRATCH_GENERAL_CFG_OFFSET,
                    PIPE_UTMI_CLOCK_SELECT | PIPE3_PHY_STATUS_SOFTWARE);
    UsbHostStallYield(200000u);
    QscratchClearBits(QSCRATCH_GENERAL_CFG_OFFSET, PIPE_UTMI_CLOCK_DISABLE);
}

static void QscratchEnableHostVbusOverride(void) {
    QscratchSetBits(QSCRATCH_SS_PHY_CTRL_OFFSET, LANE0_POWER_PRESENT);
    QscratchSetBits(QSCRATCH_HS_PHY_CTRL_OFFSET,
                    UTMI_OTG_VBUS_VALID | SOFTWARE_SESSION_VALID_SELECT);
}

int UsbHostQscratchConfigureForHost(void) {
    UINT32 General;
    UINT32 HighSpeed;
    char Line[40];
    char Hex[12];

    /* HS-only：无可用 PIPE 时钟时用 UTMI 顶上（与 maximum-speed=high-speed 同义） */
    QscratchSelectUtmiAsPipeClock();
    QscratchEnableHostVbusOverride();

    General = UsbHostReadMmio32(QSCRATCH_REGISTER_BASE + QSCRATCH_GENERAL_CFG_OFFSET);
    HighSpeed = UsbHostReadMmio32(QSCRATCH_REGISTER_BASE + QSCRATCH_HS_PHY_CTRL_OFFSET);
    UsbHostFormatHexadecimal32(Hex, General);
    UsbHostCopyText(Line, "qsc ", sizeof(Line));
    UsbHostAppendText(Line, Hex, sizeof(Line));
    BoardBringUpSplashPaintLine(208, Line);

    if ((General & (PIPE_UTMI_CLOCK_SELECT | PIPE3_PHY_STATUS_SOFTWARE)) !=
            (PIPE_UTMI_CLOCK_SELECT | PIPE3_PHY_STATUS_SOFTWARE) ||
        (HighSpeed & (UTMI_OTG_VBUS_VALID | SOFTWARE_SESSION_VALID_SELECT)) !=
            (UTMI_OTG_VBUS_VALID | SOFTWARE_SESSION_VALID_SELECT)) {
        BoardBringUpSplashPaintLine(256, "qsc bad");
        return -1;
    }
    return 0;
}

#else

int UsbHostQscratchConfigureForHost(void) {
    return -1;
}

#endif
