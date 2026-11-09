/*
 * UsbHost/EmbeddedUsbClockEnable.c — CMU_HSI0@0x11000000 开 USB2/eUSB 参考钟
 *
 * 证据：ba11110/cm433 已粘但插坞仅 cc6/ps8、c0 → 数字窗活、ESE1/HME 未上线；
 * 对表 clk-gs101.c cmu_hsi0：USB20_USER + USB20_PHY_REFCLK_26（GATE bit21）。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_PIXEL8_USB_HOST_BRINGUP
#define BOARD_PIXEL8_USB_HOST_BRINGUP 0
#endif

#if BOARD_PIXEL8_USB_HOST_BRINGUP

#define CLOCK_MANAGEMENT_UNIT_HSI0_PHYSICAL_BASE 0x11000000ULL

#define PLL_CONTROL0_USB20_USER_REGISTER 0x0640u
#define PLL_CONTROL0_USB31_USER_REGISTER 0x0650u
#define CLOCK_MUX_USB20_REFERENCE_REGISTER 0x1004u
#define CLOCK_GATE_USB_SUSPEND_REGISTER 0x2004u
#define CLOCK_GATE_PHY_CONTROL_REGISTER 0x206Cu
#define CLOCK_GATE_BUS_EARLY_REGISTER 0x2070u
#define CLOCK_GATE_USB20_PHY_REFERENCE_REGISTER 0x2074u

#define PLL_CONTROL0_SELECT_BIT (1u << 4)
#define PLL_CONTROL0_ENABLE_BIT (1u << 31)
#define PLL_CONTROL0_STABLE_BIT (1u << 29)
#define CLOCK_GATE_ENABLE_BIT (1u << 21)

static UINT8 sClockEnableStatusByte;

UINT8 UsbHostClockEnableStatusByte(void) {
    return sClockEnableStatusByte;
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

static void EnableUserMux(UINT64 Base, UINT32 Offset) {
    UINT32 Value;
    UINT32 Guard;

    Value = UsbHostReadMmio32(Base + Offset);
    Value |= PLL_CONTROL0_SELECT_BIT | PLL_CONTROL0_ENABLE_BIT;
    UsbHostWriteMmio32(Base + Offset, Value);
    for (Guard = 0; Guard < 1000u; Guard++) {
        Value = UsbHostReadMmio32(Base + Offset);
        if ((Value & PLL_CONTROL0_STABLE_BIT) != 0u) {
            break;
        }
        StallMicroseconds(10u);
    }
}

static UINT32 EnableGate(UINT64 Base, UINT32 Offset) {
    UINT32 Value;
    UINT32 Before;

    Value = UsbHostReadMmio32(Base + Offset);
    Before = Value;
    Value |= CLOCK_GATE_ENABLE_BIT;
    UsbHostWriteMmio32(Base + Offset, Value);
    return (Before & CLOCK_GATE_ENABLE_BIT) != 0u ? 1u : 0u;
}

int UsbHostClockEnableEmbeddedUsb(void) {
    UINT64 Base = CLOCK_MANAGEMENT_UNIT_HSI0_PHYSICAL_BASE;
    UINT32 Already = 0;
    UINT32 Mux;

    sClockEnableStatusByte = 0;

    HalSerialWriteShell("ck: hsi0\r\n");

    EnableUserMux(Base, PLL_CONTROL0_USB20_USER_REGISTER);
    EnableUserMux(Base, PLL_CONTROL0_USB31_USER_REGISTER);

    Mux = UsbHostReadMmio32(Base + CLOCK_MUX_USB20_REFERENCE_REGISTER);
    Mux |= 1u; /* select USB20 user path */
    UsbHostWriteMmio32(Base + CLOCK_MUX_USB20_REFERENCE_REGISTER, Mux);

    Already |= EnableGate(Base, CLOCK_GATE_USB_SUSPEND_REGISTER) << 0;
    Already |= EnableGate(Base, CLOCK_GATE_PHY_CONTROL_REGISTER) << 1;
    Already |= EnableGate(Base, CLOCK_GATE_BUS_EARLY_REGISTER) << 2;
    Already |= EnableGate(Base, CLOCK_GATE_USB20_PHY_REFERENCE_REGISTER) << 3;

    StallMicroseconds(100u);
    sClockEnableStatusByte = (UINT8)(0x10u | (Already & 0xFu));
    return 0;
}

#else

UINT8 UsbHostClockEnableStatusByte(void) {
    return 0;
}

int UsbHostClockEnableEmbeddedUsb(void) {
    return -1;
}

#endif
