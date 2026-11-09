/*
 * UsbHost/EmbeddedUsbPhysicalRetrigger.c — repeater 醒后再脉冲 phy_enable→HME
 */
#include "UsbHost/Private.h"

#ifndef BOARD_PIXEL8_USB_HOST_BRINGUP
#define BOARD_PIXEL8_USB_HOST_BRINGUP 0
#endif

#if BOARD_PIXEL8_USB_HOST_BRINGUP

#define EMBEDDED_USB_COMMON_CONTROL_REGISTER 0x04u
#define EMBEDDED_USB_ENABLE (1u << 0)
#define EMBEDDED_USB_RETENTION_ENABLE_NOT (1u << 1)
#define EMBEDDED_USB_FREQUENCY_SELECT_MASK (0x7u << 4)
#define EMBEDDED_USB_FREQUENCY_SELECT_26MHZ (0x3u << 4)
#define EMBEDDED_USB_REPEATER_MODE (1u << 10)

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

void UsbHostPhysicalRetriggerHostModeEnable(void) {
    UINT64 Base;
    UINT32 CommonControl;
    UINT32 Page;

    Page = UsbHostPhysicalFoundBasePage();
    if (Page == 0u) {
        return;
    }
    Base = ((UINT64)Page) << 12;
    CommonControl =
        UsbHostReadMmio32(Base + EMBEDDED_USB_COMMON_CONTROL_REGISTER);
    CommonControl &= ~EMBEDDED_USB_ENABLE;
    UsbHostWriteMmio32(Base + EMBEDDED_USB_COMMON_CONTROL_REGISTER,
                       CommonControl);
    StallMicroseconds(100u);
    CommonControl |= EMBEDDED_USB_ENABLE | EMBEDDED_USB_REPEATER_MODE |
                     EMBEDDED_USB_RETENTION_ENABLE_NOT;
    CommonControl = (CommonControl & ~EMBEDDED_USB_FREQUENCY_SELECT_MASK) |
                    EMBEDDED_USB_FREQUENCY_SELECT_26MHZ;
    UsbHostWriteMmio32(Base + EMBEDDED_USB_COMMON_CONTROL_REGISTER,
                       CommonControl);
    StallMicroseconds(1000u);
    StallMicroseconds(28u);
    StallMicroseconds(2500u);
    UsbHostPhysicalRefreshCommonControl();
}

#else

void UsbHostPhysicalRetriggerHostModeEnable(void) {
}

#endif
