/*
 * UsbHost/EmbeddedUsbEssentialLink.c — USBCON@0x11100000 LINKCTRL / UTMI
 *
 * 对表 exynos-usb-blkcon-sfr：Host 旁路 bus filter、force QACT、force BVALID。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_PIXEL8_USB_HOST_BRINGUP
#define BOARD_PIXEL8_USB_HOST_BRINGUP 0
#endif

#if BOARD_PIXEL8_USB_HOST_BRINGUP

#define ESSENTIAL_CONTROL_PHYSICAL_BASE 0x11100000ULL
#define USB_CONNECTION_LINK_CONTROL_REGISTER 0x04u
#define USB_CONNECTION_LINK_CLOCK_RESET_REGISTER 0x0Cu
#define USB_CONNECTION_UTMI_CONTROL_REGISTER 0x10u
#define USB_CONNECTION_LINK_SOFTWARE_RESET (1u << 0)
#define USB_CONNECTION_BUS_FILTER_BYPASS_MASK (0xFu << 4)
#define USB_CONNECTION_BUS_FILTER_BYPASS_HOST (0xFu << 4)
#define USB_CONNECTION_FORCE_QUEUE_ACTIVE (1u << 8)
#define USB_CONNECTION_DISABLE_BVALID_QUEUE_ACTIVE (1u << 9)
#define USB_CONNECTION_DISABLE_VBUSVALID_QUEUE_ACTIVE (1u << 10)
#define USB_CONNECTION_DISABLE_IDENTIFIER_QUEUE_ACTIVE (1u << 11)
#define USB_CONNECTION_DISABLE_LINK_GATE_QUEUE_ACTIVE (1u << 12)
#define USB_CONNECTION_SELECT_START_OF_FRAME_MASK (0x3u << 20)
#define USB_CONNECTION_SELECT_START_OF_FRAME_ONE (0x1u << 20)
#define USB_CONNECTION_FORCE_BVALID (1u << 0)
#define USB_CONNECTION_FORCE_VBUSVALID (1u << 1)

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

void UsbHostEssentialControlLinkInitialize(void) {
    UINT32 LinkControl;
    UINT32 ClockReset;
    UINT32 UtmiControl;

    LinkControl =
        UsbHostReadMmio32(ESSENTIAL_CONTROL_PHYSICAL_BASE +
                          USB_CONNECTION_LINK_CONTROL_REGISTER);
    LinkControl |= USB_CONNECTION_DISABLE_BVALID_QUEUE_ACTIVE |
                   USB_CONNECTION_DISABLE_VBUSVALID_QUEUE_ACTIVE |
                   USB_CONNECTION_DISABLE_IDENTIFIER_QUEUE_ACTIVE |
                   USB_CONNECTION_DISABLE_LINK_GATE_QUEUE_ACTIVE;
    LinkControl &= ~USB_CONNECTION_FORCE_QUEUE_ACTIVE;
    UsbHostWriteMmio32(ESSENTIAL_CONTROL_PHYSICAL_BASE +
                           USB_CONNECTION_LINK_CONTROL_REGISTER,
                       LinkControl);
    StallMicroseconds(500u);
    LinkControl |= USB_CONNECTION_FORCE_QUEUE_ACTIVE;
    LinkControl = (LinkControl & ~USB_CONNECTION_BUS_FILTER_BYPASS_MASK) |
                  USB_CONNECTION_BUS_FILTER_BYPASS_HOST;
    LinkControl = (LinkControl & ~USB_CONNECTION_SELECT_START_OF_FRAME_MASK) |
                  USB_CONNECTION_SELECT_START_OF_FRAME_ONE;
    UsbHostWriteMmio32(ESSENTIAL_CONTROL_PHYSICAL_BASE +
                           USB_CONNECTION_LINK_CONTROL_REGISTER,
                       LinkControl);

    ClockReset = UsbHostReadMmio32(ESSENTIAL_CONTROL_PHYSICAL_BASE +
                                   USB_CONNECTION_LINK_CLOCK_RESET_REGISTER);
    ClockReset |= USB_CONNECTION_LINK_SOFTWARE_RESET;
    UsbHostWriteMmio32(ESSENTIAL_CONTROL_PHYSICAL_BASE +
                           USB_CONNECTION_LINK_CLOCK_RESET_REGISTER,
                       ClockReset);
    StallMicroseconds(10u);
    ClockReset &= ~USB_CONNECTION_LINK_SOFTWARE_RESET;
    UsbHostWriteMmio32(ESSENTIAL_CONTROL_PHYSICAL_BASE +
                           USB_CONNECTION_LINK_CLOCK_RESET_REGISTER,
                       ClockReset);

    UtmiControl = UsbHostReadMmio32(ESSENTIAL_CONTROL_PHYSICAL_BASE +
                                    USB_CONNECTION_UTMI_CONTROL_REGISTER);
    UtmiControl |=
        USB_CONNECTION_FORCE_BVALID | USB_CONNECTION_FORCE_VBUSVALID;
    UsbHostWriteMmio32(ESSENTIAL_CONTROL_PHYSICAL_BASE +
                           USB_CONNECTION_UTMI_CONTROL_REGISTER,
                       UtmiControl);
}

#else

void UsbHostEssentialControlLinkInitialize(void) {
}

#endif
