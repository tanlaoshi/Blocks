/*
 * UsbHost/EmbeddedUsbPhysical.c — zuma eUSB CON
 * ba11100+cm8000432＝USBCON 假 RPTR；探针须 RPTR+ENABLE 双粘。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_PIXEL8_USB_HOST_BRINGUP
#define BOARD_PIXEL8_USB_HOST_BRINGUP 0
#endif

#if BOARD_PIXEL8_USB_HOST_BRINGUP

#define EMBEDDED_USB_CONTROL_PHYSICAL_BASE 0x11110000ULL /* 真机 ba≈11110；cm433 */

#define EMBEDDED_USB_RESET_CONTROL_REGISTER 0x00u
#define EMBEDDED_USB_COMMON_CONTROL_REGISTER 0x04u
#define EMBEDDED_USB_PLL_CONFIGURATION0_REGISTER 0x08u
#define EMBEDDED_USB_PLL_CONFIGURATION1_REGISTER 0x0Cu
#define EMBEDDED_USB_UTMI_REGISTER 0x1Cu
#define EMBEDDED_USB_TEST_REGISTER 0x20u
#define EMBEDDED_USB_UTMI_DM_PULLDOWN (1u << 1)
#define EMBEDDED_USB_UTMI_DM_PULLDOWN_OVERRIDE (1u << 2)
#define EMBEDDED_USB_UTMI_DP_PULLDOWN (1u << 3)
#define EMBEDDED_USB_UTMI_DP_PULLDOWN_OVERRIDE (1u << 4)

#define EMBEDDED_USB_PHY_RESET (1u << 0)
#define EMBEDDED_USB_PHY_RESET_OVERRIDE_ENABLE (1u << 1)
#define EMBEDDED_USB_UTMI_PORT_RESET (1u << 4)
#define EMBEDDED_USB_UTMI_PORT_RESET_OVERRIDE_ENABLE (1u << 5)

#define EMBEDDED_USB_ENABLE (1u << 0)
#define EMBEDDED_USB_RETENTION_ENABLE_NOT (1u << 1)
#define EMBEDDED_USB_FREQUENCY_SELECT_MASK (0x7u << 4)
#define EMBEDDED_USB_FREQUENCY_SELECT_26MHZ (0x3u << 4)
#define EMBEDDED_USB_REPEATER_MODE (1u << 10)
#define EMBEDDED_USB_PLL_FEEDBACK_DIVIDER_MASK (0xFFFu << 8)
#define EMBEDDED_USB_PLL_FEEDBACK_DIVIDER_26MHZ (263u << 8)
#define EMBEDDED_USB_TEST_IDDQ (1u << 6)

#define EMBEDDED_USB_STRAP_VALUE                                                 \
    (EMBEDDED_USB_RETENTION_ENABLE_NOT | EMBEDDED_USB_FREQUENCY_SELECT_26MHZ |  \
     EMBEDDED_USB_REPEATER_MODE)

static UINT8 sPhysicalLastResultCode;
static UINT32 sPhysicalCommonControl;
static UINT32 sPhysicalStrapControl;
static UINT32 sPhysicalFoundBasePage;

UINT8 UsbHostPhysicalLastResultCode(void) {
    return sPhysicalLastResultCode;
}

UINT32 UsbHostPhysicalCommonControl(void) {
    return sPhysicalCommonControl;
}

UINT32 UsbHostPhysicalStrapControl(void) {
    return sPhysicalStrapControl;
}

UINT32 UsbHostPhysicalFoundBasePage(void) {
    return sPhysicalFoundBasePage;
}

void UsbHostPhysicalRefreshCommonControl(void) {
    if (sPhysicalFoundBasePage == 0u) {
        return;
    }
    sPhysicalCommonControl = UsbHostReadMmio32(
        (((UINT64)sPhysicalFoundBasePage) << 12) +
        EMBEDDED_USB_COMMON_CONTROL_REGISTER);
}

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

    ResetControl = EMBEDDED_USB_PHY_RESET | EMBEDDED_USB_PHY_RESET_OVERRIDE_ENABLE |
                   EMBEDDED_USB_UTMI_PORT_RESET |
                   EMBEDDED_USB_UTMI_PORT_RESET_OVERRIDE_ENABLE;
    UsbHostWriteMmio32(Base + EMBEDDED_USB_RESET_CONTROL_REGISTER, ResetControl);

    UsbHostWriteMmio32(Base + EMBEDDED_USB_COMMON_CONTROL_REGISTER,
                       EMBEDDED_USB_STRAP_VALUE);
    sPhysicalStrapControl =
        UsbHostReadMmio32(Base + EMBEDDED_USB_COMMON_CONTROL_REGISTER);

    WriteMasked(Base, EMBEDDED_USB_PLL_CONFIGURATION0_REGISTER,
                EMBEDDED_USB_PLL_FEEDBACK_DIVIDER_MASK,
                EMBEDDED_USB_PLL_FEEDBACK_DIVIDER_26MHZ);
    WriteMasked(Base, EMBEDDED_USB_PLL_CONFIGURATION1_REGISTER, 0xFu, 0u);
    WriteMasked(Base, EMBEDDED_USB_TEST_REGISTER, EMBEDDED_USB_TEST_IDDQ, 0u);
    StallMicroseconds(10u);

    ResetControl =
        UsbHostReadMmio32(Base + EMBEDDED_USB_RESET_CONTROL_REGISTER);
    ResetControl &= ~EMBEDDED_USB_PHY_RESET;
    UsbHostWriteMmio32(Base + EMBEDDED_USB_RESET_CONTROL_REGISTER, ResetControl);
    StallMicroseconds(10u);

    /* Host：DP/DM pulldown（对表 eUSB CON UTMI） */
    WriteMasked(Base, EMBEDDED_USB_UTMI_REGISTER,
                EMBEDDED_USB_UTMI_DM_PULLDOWN |
                    EMBEDDED_USB_UTMI_DM_PULLDOWN_OVERRIDE |
                    EMBEDDED_USB_UTMI_DP_PULLDOWN |
                    EMBEDDED_USB_UTMI_DP_PULLDOWN_OVERRIDE,
                EMBEDDED_USB_UTMI_DM_PULLDOWN |
                    EMBEDDED_USB_UTMI_DM_PULLDOWN_OVERRIDE |
                    EMBEDDED_USB_UTMI_DP_PULLDOWN |
                    EMBEDDED_USB_UTMI_DP_PULLDOWN_OVERRIDE);

    CommonControl =
        UsbHostReadMmio32(Base + EMBEDDED_USB_COMMON_CONTROL_REGISTER);
    CommonControl |= EMBEDDED_USB_ENABLE | EMBEDDED_USB_REPEATER_MODE |
                     EMBEDDED_USB_RETENTION_ENABLE_NOT;
    CommonControl = (CommonControl & ~EMBEDDED_USB_FREQUENCY_SELECT_MASK) |
                    EMBEDDED_USB_FREQUENCY_SELECT_26MHZ;
    UsbHostWriteMmio32(Base + EMBEDDED_USB_COMMON_CONTROL_REGISTER,
                       CommonControl);

    /* 对表 phy-snps-eusb2：ENABLE 后即放 UTMI；再等 ESE1/HME */
    ResetControl =
        UsbHostReadMmio32(Base + EMBEDDED_USB_RESET_CONTROL_REGISTER);
    ResetControl &= ~(EMBEDDED_USB_UTMI_PORT_RESET |
                      EMBEDDED_USB_UTMI_PORT_RESET_OVERRIDE_ENABLE);
    UsbHostWriteMmio32(Base + EMBEDDED_USB_RESET_CONTROL_REGISTER, ResetControl);
    StallMicroseconds(1000u);
    StallMicroseconds(28u);
    StallMicroseconds(2500u);

    CommonControl =
        UsbHostReadMmio32(Base + EMBEDDED_USB_COMMON_CONTROL_REGISTER);
    CommonControl |= EMBEDDED_USB_ENABLE | EMBEDDED_USB_REPEATER_MODE;
    UsbHostWriteMmio32(Base + EMBEDDED_USB_COMMON_CONTROL_REGISTER,
                       CommonControl);
    StallMicroseconds(100u);

    sPhysicalCommonControl =
        UsbHostReadMmio32(Base + EMBEDDED_USB_COMMON_CONTROL_REGISTER);
}

int UsbHostPhysicalEnableRepeaterMode(void) {
    UINT64 Chosen = EMBEDDED_USB_CONTROL_PHYSICAL_BASE;
    UINT32 Strap = 0;
    UINT32 Common = 0;

    sPhysicalLastResultCode = 0;
    sPhysicalCommonControl = 0;
    sPhysicalStrapControl = 0;
    sPhysicalFoundBasePage = 0;

    HalSerialWriteShell("ph: ess\r\n");
    UsbHostEssentialControlLinkInitialize();

    if (ProbeEmbeddedUsbEnableSticky(Chosen, &Strap, &Common) != 0) {
        static const UINT64 Fallback[] = {
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
        sPhysicalLastResultCode = 0x13u;
        HalSerialWriteShell("ph: no en\r\n");
        return -1;
    }

    sPhysicalFoundBasePage = (UINT32)(Chosen >> 12);
    sPhysicalStrapControl = Strap;
    sPhysicalCommonControl = Common;
    HalSerialWriteShell("ph: init\r\n");
    InitializeEmbeddedUsbAtBase(Chosen);
    sPhysicalLastResultCode = 1u;
    return 0;
}

#else

UINT8 UsbHostPhysicalLastResultCode(void) {
    return 0;
}

UINT32 UsbHostPhysicalCommonControl(void) {
    return 0;
}

UINT32 UsbHostPhysicalStrapControl(void) {
    return 0;
}

UINT32 UsbHostPhysicalFoundBasePage(void) {
    return 0;
}

void UsbHostPhysicalRefreshCommonControl(void) {
}

int UsbHostPhysicalEnableRepeaterMode(void) {
    return -1;
}

#endif
