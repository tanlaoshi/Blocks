/*
 * UsbHost/TypeCAnalogSwitch.c — 切回 USB 数据通路（WCD USBC analog idle）
 *
 * 证据：CC attach + VCONN + OTG 已成，仍 `ls80`/`p*:200` → D+/D- 未到 QUSB。
 * chiron DT：`wcd_usbc_ana_en1_idle`=gpio59 输出低；`en2n_idle`=gpio60 输出高。
 * 对表 pinctrl-msm8998：WEST tile + mux=gpio / OE / OUT。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_MIX2_USB_HOST_BRINGUP
#define BOARD_MIX2_USB_HOST_BRINGUP 0
#endif

#if BOARD_MIX2_USB_HOST_BRINGUP

#define TLMM_REGISTER_BASE                       0x03400000ULL
#define TLMM_WEST_TILE_OFFSET                    0x00100000ULL
#define TLMM_GPIO_STRIDE                         0x1000u
#define TLMM_MUX_SHIFT                           2u
#define TLMM_PULL_SHIFT                          0u
#define TLMM_DRIVE_SHIFT                         6u
#define TLMM_OUTPUT_ENABLE_SHIFT                 9u
#define TLMM_PULL_DOWN                           1u
#define TLMM_PULL_DISABLE                        0u
#define TLMM_DRIVE_2_MILLIAMPERE                 0u
#define TLMM_GPIO_FUNCTION                       0u
#define TLMM_IO_OUTPUT_SHIFT                     1u
#define TYPE_C_ANALOG_ENABLE1_GPIO               59u
#define TYPE_C_ANALOG_ENABLE2_ACTIVE_LOW_GPIO    60u
#define SSPHY_VLS_CLAMP_ADDRESS                 0x01FCB244ULL

static UINT8 sAnalogSwitchByte;

static UINT64 GpioControlAddress(UINT32 GpioNumber) {
    return TLMM_REGISTER_BASE + TLMM_WEST_TILE_OFFSET +
           ((UINT64)GpioNumber * (UINT64)TLMM_GPIO_STRIDE);
}

static UINT64 GpioInputOutputAddress(UINT32 GpioNumber) {
    return GpioControlAddress(GpioNumber) + 4u;
}

static void ConfigureGpioOutput(UINT32 GpioNumber, UINT32 Pull,
                                UINT32 OutputHigh) {
    UINT32 Control;
    UINT32 InputOutput;

    Control = (TLMM_GPIO_FUNCTION << TLMM_MUX_SHIFT) |
              (Pull << TLMM_PULL_SHIFT) |
              (TLMM_DRIVE_2_MILLIAMPERE << TLMM_DRIVE_SHIFT) |
              (1u << TLMM_OUTPUT_ENABLE_SHIFT);
    UsbHostWriteMmio32(GpioControlAddress(GpioNumber), Control);

    InputOutput = UsbHostReadMmio32(GpioInputOutputAddress(GpioNumber));
    if (OutputHigh != 0) {
        InputOutput |= (1u << TLMM_IO_OUTPUT_SHIFT);
    } else {
        InputOutput &= ~(1u << TLMM_IO_OUTPUT_SHIFT);
    }
    UsbHostWriteMmio32(GpioInputOutputAddress(GpioNumber), InputOutput);
}

UINT8 UsbHostTypeCAnalogSwitchByte(void) {
    UINT32 Enable1;
    UINT32 Enable2;

    Enable1 = UsbHostReadMmio32(
                  GpioInputOutputAddress(TYPE_C_ANALOG_ENABLE1_GPIO)) >>
              TLMM_IO_OUTPUT_SHIFT;
    Enable2 = UsbHostReadMmio32(GpioInputOutputAddress(
                  TYPE_C_ANALOG_ENABLE2_ACTIVE_LOW_GPIO)) >>
              TLMM_IO_OUTPUT_SHIFT;
    sAnalogSwitchByte =
        (UINT8)(((Enable1 & 1u) << 0) | ((Enable2 & 1u) << 1));
    return sAnalogSwitchByte;
}

int UsbHostTypeCAnalogSwitchForceUsb(void) {
    /* USB idle：en1=0、en2n=1；并松开 SS PHY VLS clamp */
    ConfigureGpioOutput(TYPE_C_ANALOG_ENABLE1_GPIO, TLMM_PULL_DOWN, 0);
    ConfigureGpioOutput(TYPE_C_ANALOG_ENABLE2_ACTIVE_LOW_GPIO,
                        TLMM_PULL_DISABLE, 1);
    UsbHostWriteMmio32(SSPHY_VLS_CLAMP_ADDRESS, 0);
    (void)UsbHostTypeCAnalogSwitchByte();
    return (sAnalogSwitchByte == 0x02) ? 0 : -1;
}

#else

UINT8 UsbHostTypeCAnalogSwitchByte(void) {
    return 0;
}

int UsbHostTypeCAnalogSwitchForceUsb(void) {
    return -1;
}

#endif
