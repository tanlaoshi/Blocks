/*
 * UsbHost/EmbeddedUsbPhysical.c — zuma eUSB CON@0x11110000
 *
 * 对表 phy_exynos_eusb_initiate：ENABLE→T4/T5→放 utmi；ref 19.2MHz。
 * 状态字见 EmbeddedUsbPhysicalStatus.c。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_PIXEL8_USB_HOST_BRINGUP
#define BOARD_PIXEL8_USB_HOST_BRINGUP 0
#endif

#if BOARD_PIXEL8_USB_HOST_BRINGUP

#define EMBEDDED_USB_CONTROL_PHYSICAL_BASE 0x11110000ULL
#define EMBEDDED_USB_RESET_CONTROL_REGISTER 0x00u
#define EMBEDDED_USB_COMMON_CONTROL_REGISTER 0x04u
#define EMBEDDED_USB_PLL_CONFIGURATION0_REGISTER 0x08u
#define EMBEDDED_USB_PLL_CONFIGURATION1_REGISTER 0x0Cu
#define EMBEDDED_USB_TRANSMIT_TUNE_REGISTER 0x14u
#define EMBEDDED_USB_TEST_REGISTER 0x20u
#define EMBEDDED_USB_PHYSICAL_DIGITAL_BASE 0x11120000ULL
#define EMBEDDED_USB_STATE_JUMP_DEBUG_OUT_REGISTER 0x15Cu
#define EMBEDDED_USB_FULL_SPEED_VOLTAGE_REFERENCE_TUNE_MASK (3u << 1)
#define EMBEDDED_USB_PHY_RESET (1u << 0)
#define EMBEDDED_USB_PHY_RESET_OVERRIDE_ENABLE (1u << 1)
#define EMBEDDED_USB_UTMI_PORT_RESET (1u << 4)
#define EMBEDDED_USB_UTMI_PORT_RESET_OVERRIDE_ENABLE (1u << 5)
#define EMBEDDED_USB_ENABLE (1u << 0)
#define EMBEDDED_USB_RETENTION_ENABLE_NOT (1u << 1)
#define EMBEDDED_USB_FREQUENCY_SELECT_MASK (0x7u << 4)
#define EMBEDDED_USB_FREQUENCY_SELECT_19200KHZ (0x0u << 4)
#define EMBEDDED_USB_REPEATER_MODE (1u << 10)
#define EMBEDDED_USB_PLL_FEEDBACK_DIVIDER_MASK (0xFFFu << 8)
#define EMBEDDED_USB_PLL_FEEDBACK_DIVIDER_19200KHZ (368u << 8)
#define EMBEDDED_USB_PLL_CHARGE_PUMP_BIAS_MASK 0x7Fu
#define EMBEDDED_USB_PLL_CHARGE_PUMP_BIAS_19200KHZ 1u
#define EMBEDDED_USB_PLL_REFERENCE_DIVIDER_MASK 0xFu
#define EMBEDDED_USB_TEST_IDDQ (1u << 6)

#define EMBEDDED_USB_STRAP_VALUE                                                 \
    (EMBEDDED_USB_RETENTION_ENABLE_NOT | EMBEDDED_USB_FREQUENCY_SELECT_19200KHZ | \
     EMBEDDED_USB_REPEATER_MODE)

static void StallMicroseconds(UINT32 Microseconds) {
    UINT64 Frequency;
    UINT64 Start;
    UINT64 Delta;

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(Frequency));
    if (Frequency == 0u) {
        UsbHostStallYield((UINTN)Microseconds * 100u);
        return;
    }
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(Start));
    Delta = (Frequency * (UINT64)Microseconds) / 1000000ull;
    for (;;) {
        UINT64 Now;
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(Now));
        if ((Now - Start) >= Delta) {
            break;
        }
    }
}

static void WriteMasked(UINT64 Base, UINT32 Offset, UINT32 Mask, UINT32 Value) {
    UINT32 RegisterValue;

    RegisterValue = UsbHostReadMmio32(Base + Offset);
    RegisterValue = (RegisterValue & ~Mask) | (Value & Mask);
    UsbHostWriteMmio32(Base + Offset, RegisterValue);
}

static int ProbeEmbeddedUsbEnableSticky(UINT64 Base, UINT32 *StrapOut,
                                        UINT32 *CommonOut) {
    UINT32 ResetControl;
    UINT32 Strap;
    UINT32 Common;
    UINT32 Dead;

    Dead = UsbHostReadMmio32(Base + EMBEDDED_USB_COMMON_CONTROL_REGISTER);
    if (Dead == 0xFFFFFFFFu || Dead == 0u) {
        return -1;
    }

    ResetControl = EMBEDDED_USB_PHY_RESET | EMBEDDED_USB_PHY_RESET_OVERRIDE_ENABLE |
                   EMBEDDED_USB_UTMI_PORT_RESET |
                   EMBEDDED_USB_UTMI_PORT_RESET_OVERRIDE_ENABLE;
    UsbHostWriteMmio32(Base + EMBEDDED_USB_RESET_CONTROL_REGISTER, ResetControl);
    StallMicroseconds(10u);

    UsbHostWriteMmio32(Base + EMBEDDED_USB_COMMON_CONTROL_REGISTER,
                       EMBEDDED_USB_STRAP_VALUE);
    StallMicroseconds(10u);
    Strap = UsbHostReadMmio32(Base + EMBEDDED_USB_COMMON_CONTROL_REGISTER);
    if ((Strap & EMBEDDED_USB_REPEATER_MODE) == 0u) {
        return -1;
    }

    WriteMasked(Base, EMBEDDED_USB_TEST_REGISTER, EMBEDDED_USB_TEST_IDDQ, 0u);
    StallMicroseconds(10u);

    ResetControl =
        UsbHostReadMmio32(Base + EMBEDDED_USB_RESET_CONTROL_REGISTER);
    ResetControl &= ~EMBEDDED_USB_PHY_RESET;
    UsbHostWriteMmio32(Base + EMBEDDED_USB_RESET_CONTROL_REGISTER, ResetControl);
    StallMicroseconds(10u);

    Common = Strap | EMBEDDED_USB_ENABLE;
    UsbHostWriteMmio32(Base + EMBEDDED_USB_COMMON_CONTROL_REGISTER, Common);
    StallMicroseconds(20u);
    Common = UsbHostReadMmio32(Base + EMBEDDED_USB_COMMON_CONTROL_REGISTER);
    if ((Common & EMBEDDED_USB_ENABLE) == 0u) {
        return -1;
    }
    if ((Common & EMBEDDED_USB_REPEATER_MODE) == 0u) {
        return -1;
    }

    if (StrapOut != 0) {
        *StrapOut = Strap;
    }
    if (CommonOut != 0) {
        *CommonOut = Common;
    }
    return 0;
}

static void InitializeEmbeddedUsbAtBase(UINT64 Base) {
    UINT32 ResetControl;
    UINT32 CommonControl;
    UINT32 Strap;

    /* 持 phy+utmi reset → strap/PLL → enable=0 → 放 phy → enable=1 → T4/T5 → 放 utmi */
    ResetControl = EMBEDDED_USB_PHY_RESET | EMBEDDED_USB_PHY_RESET_OVERRIDE_ENABLE |
                   EMBEDDED_USB_UTMI_PORT_RESET |
                   EMBEDDED_USB_UTMI_PORT_RESET_OVERRIDE_ENABLE;
    UsbHostWriteMmio32(Base + EMBEDDED_USB_RESET_CONTROL_REGISTER, ResetControl);
    UsbHostWriteMmio32(Base + EMBEDDED_USB_COMMON_CONTROL_REGISTER,
                       EMBEDDED_USB_STRAP_VALUE);
    Strap = UsbHostReadMmio32(Base + EMBEDDED_USB_COMMON_CONTROL_REGISTER);
    WriteMasked(Base, EMBEDDED_USB_PLL_CONFIGURATION0_REGISTER,
                EMBEDDED_USB_PLL_FEEDBACK_DIVIDER_MASK |
                    EMBEDDED_USB_PLL_CHARGE_PUMP_BIAS_MASK,
                EMBEDDED_USB_PLL_FEEDBACK_DIVIDER_19200KHZ |
                    EMBEDDED_USB_PLL_CHARGE_PUMP_BIAS_19200KHZ);
    WriteMasked(Base, EMBEDDED_USB_PLL_CONFIGURATION1_REGISTER,
                EMBEDDED_USB_PLL_REFERENCE_DIVIDER_MASK, 0u);
    /* 对表 phy_exynos_eusb_initiate：fsls_vref_tune=0；勿强灌 UTMI pulldown */
    WriteMasked(Base, EMBEDDED_USB_TRANSMIT_TUNE_REGISTER,
                EMBEDDED_USB_FULL_SPEED_VOLTAGE_REFERENCE_TUNE_MASK, 0u);
    WriteMasked(Base, EMBEDDED_USB_TEST_REGISTER, EMBEDDED_USB_TEST_IDDQ, 0u);
    CommonControl =
        UsbHostReadMmio32(Base + EMBEDDED_USB_COMMON_CONTROL_REGISTER);
    CommonControl &= ~EMBEDDED_USB_ENABLE;
    UsbHostWriteMmio32(Base + EMBEDDED_USB_COMMON_CONTROL_REGISTER,
                       CommonControl);
    StallMicroseconds(10u);
    ResetControl =
        UsbHostReadMmio32(Base + EMBEDDED_USB_RESET_CONTROL_REGISTER);
    ResetControl &= ~EMBEDDED_USB_PHY_RESET;
    UsbHostWriteMmio32(Base + EMBEDDED_USB_RESET_CONTROL_REGISTER, ResetControl);
    StallMicroseconds(10u);
    CommonControl =
        UsbHostReadMmio32(Base + EMBEDDED_USB_COMMON_CONTROL_REGISTER);
    CommonControl |= EMBEDDED_USB_ENABLE | EMBEDDED_USB_REPEATER_MODE |
                     EMBEDDED_USB_RETENTION_ENABLE_NOT;
    CommonControl = (CommonControl & ~EMBEDDED_USB_FREQUENCY_SELECT_MASK) |
                    EMBEDDED_USB_FREQUENCY_SELECT_19200KHZ;
    UsbHostWriteMmio32(Base + EMBEDDED_USB_COMMON_CONTROL_REGISTER,
                       CommonControl);
    StallMicroseconds(1000u);
    StallMicroseconds(28u);
    StallMicroseconds(2500u);
    ResetControl =
        UsbHostReadMmio32(Base + EMBEDDED_USB_RESET_CONTROL_REGISTER);
    ResetControl &= ~(EMBEDDED_USB_UTMI_PORT_RESET |
                      EMBEDDED_USB_UTMI_PORT_RESET_OVERRIDE_ENABLE);
    UsbHostWriteMmio32(Base + EMBEDDED_USB_RESET_CONTROL_REGISTER, ResetControl);
    StallMicroseconds(100u);
    CommonControl =
        UsbHostReadMmio32(Base + EMBEDDED_USB_COMMON_CONTROL_REGISTER);
    UsbHostPhysicalStatusSetStrapAndCommon(Strap, CommonControl);
    UsbHostPhysicalStatusSetEmbeddedUsbState((UINT8)(UsbHostReadMmio32(
        EMBEDDED_USB_PHYSICAL_DIGITAL_BASE +
        EMBEDDED_USB_STATE_JUMP_DEBUG_OUT_REGISTER) &
                                                     0xFFu));
}

int UsbHostPhysicalEnableRepeaterMode(void) {
    UINT64 Chosen = EMBEDDED_USB_CONTROL_PHYSICAL_BASE;
    UINT32 Strap = 0;
    UINT32 Common = 0;

    UsbHostPhysicalStatusClear();

    HalSerialWriteShell("ph: dr\r\n");
    (void)UsbHostDualRoleDevicePhysicalInitialize();

    if (ProbeEmbeddedUsbEnableSticky(Chosen, &Strap, &Common) != 0) {
        static const UINT64 Fallback[] = {
            0x110F0000ULL, /* 社区 gs201 MEM1 eusb_ctrl 候选 */
            0x11111000ULL, 0x111c0000ULL, 0x10ab0000ULL, 0x11120000ULL,
        };
        UINTN Index;
        Chosen = 0;
        for (Index = 0; Index < sizeof(Fallback) / sizeof(Fallback[0]);
             Index++) {
            if (ProbeEmbeddedUsbEnableSticky(Fallback[Index], &Strap, &Common) ==
                0) {
                Chosen = Fallback[Index];
                break;
            }
        }
    }
    if (Chosen == 0) {
        UsbHostPhysicalStatusSetResultCode(0x13u);
        HalSerialWriteShell("ph: no en\r\n");
        return -1;
    }

    UsbHostPhysicalStatusSetFoundBasePage((UINT32)(Chosen >> 12));
    UsbHostPhysicalStatusSetStrapAndCommon(Strap, Common);
    HalSerialWriteShell("ph: init\r\n");
    InitializeEmbeddedUsbAtBase(Chosen);
    UsbHostPhysicalStatusSetResultCode(1u);
    UsbHostPhysicalRefreshCommonControl();
    return 0;
}

#else

int UsbHostPhysicalEnableRepeaterMode(void) {
    return -1;
}

#endif
