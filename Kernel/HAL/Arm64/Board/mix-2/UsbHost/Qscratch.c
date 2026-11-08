/*
 * UsbHost/Qscratch.c — ssusb 第二段 QSCRATCH @0xC016000
 *
 * 证据：chiron DT `ssusb@a800000` reg[1]=0xc016000；`dwc3` maximum-speed=high-speed；
 * `ssphy` 有 `qcom,vbus-valid-override`。
 * 对表 torvalds `dwc3-qcom.c`：select_utmi_clk **只在 probe 一次**；
 * host 侧 VBUS/SESSVLD 可在角色切换时重刷。
 * 反例：CCS/复位路径再跑 CLOCK_DISABLE → HS chirp 卡 Polling（真机 e10e1）。
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

static UINT8 sUtmiAsPipeClockSelected;
static UINT32 sQscratchGeneralConfiguration;

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

static void QscratchSelectUtmiAsPipeClockOnce(void) {
    if (sUtmiAsPipeClockSelected != 0) {
        return;
    }
    /* 对表 dwc3_qcom_select_utmi_clk：DISABLE → 选 UTMI → 清 DISABLE */
    QscratchSetBits(QSCRATCH_GENERAL_CFG_OFFSET, PIPE_UTMI_CLOCK_DISABLE);
    UsbHostStallYield(200000u);
    QscratchSetBits(QSCRATCH_GENERAL_CFG_OFFSET,
                    PIPE_UTMI_CLOCK_SELECT | PIPE3_PHY_STATUS_SOFTWARE);
    UsbHostStallYield(200000u);
    QscratchClearBits(QSCRATCH_GENERAL_CFG_OFFSET, PIPE_UTMI_CLOCK_DISABLE);
    sUtmiAsPipeClockSelected = 1;
}

static void QscratchEnableHostVbusOverride(void) {
    QscratchSetBits(QSCRATCH_SS_PHY_CTRL_OFFSET, LANE0_POWER_PRESENT);
    QscratchSetBits(QSCRATCH_HS_PHY_CTRL_OFFSET,
                    UTMI_OTG_VBUS_VALID | SOFTWARE_SESSION_VALID_SELECT);
}

static int QscratchReadbackHostOk(void) {
    UINT32 General;
    UINT32 HighSpeed;

    General = UsbHostReadMmio32(QSCRATCH_REGISTER_BASE + QSCRATCH_GENERAL_CFG_OFFSET);
    HighSpeed = UsbHostReadMmio32(QSCRATCH_REGISTER_BASE + QSCRATCH_HS_PHY_CTRL_OFFSET);
    sQscratchGeneralConfiguration = General;
    if ((General & (PIPE_UTMI_CLOCK_SELECT | PIPE3_PHY_STATUS_SOFTWARE)) !=
            (PIPE_UTMI_CLOCK_SELECT | PIPE3_PHY_STATUS_SOFTWARE) ||
        (HighSpeed & (UTMI_OTG_VBUS_VALID | SOFTWARE_SESSION_VALID_SELECT)) !=
            (UTMI_OTG_VBUS_VALID | SOFTWARE_SESSION_VALID_SELECT)) {
        return -1;
    }
    return 0;
}

UINT32 UsbHostQscratchGeneralConfiguration(void) {
    return sQscratchGeneralConfiguration;
}

int UsbHostQscratchConfigureForHost(void) {
    /* HS-only：无可用 PIPE 时钟时用 UTMI 顶上（与 maximum-speed=high-speed 同义） */
    QscratchSelectUtmiAsPipeClockOnce();
    QscratchEnableHostVbusOverride();
    return QscratchReadbackHostOk();
}

/*
 * Attach/复位路径：只保 VBUS/SESSVLD，禁止再 DISABLE PIPE 时钟。
 */
int UsbHostQscratchEnsureHostSessionOverride(void) {
    QscratchEnableHostVbusOverride();
    return QscratchReadbackHostOk();
}

#else

UINT32 UsbHostQscratchGeneralConfiguration(void) {
    return 0;
}

int UsbHostQscratchConfigureForHost(void) {
    return -1;
}

int UsbHostQscratchEnsureHostSessionOverride(void) {
    return -1;
}

#endif
