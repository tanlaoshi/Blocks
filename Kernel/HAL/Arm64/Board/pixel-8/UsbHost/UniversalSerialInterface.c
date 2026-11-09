/*
 * UsbHost/UniversalSerialInterface.c — zuma USI@10cb00c0 出复位 + CLKREQ
 *
 * 对表 exynos-usi.c：清 USI_CON reset；OPTION CLKREQ_ON / 清 CLKSTOP。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_PIXEL8_USB_HOST_BRINGUP
#define BOARD_PIXEL8_USB_HOST_BRINGUP 0
#endif

#if BOARD_PIXEL8_USB_HOST_BRINGUP

#define UNIVERSAL_SERIAL_INTERFACE_PHYSICAL 0x10CB00C0ULL
#define UNIVERSAL_SERIAL_INTERFACE_CONTROL  0x04u
#define UNIVERSAL_SERIAL_INTERFACE_OPTION   0x08u
#define UNIVERSAL_SERIAL_INTERFACE_RESET_BIT (1u << 0)
#define UNIVERSAL_SERIAL_INTERFACE_CLOCK_REQUEST_ON (1u << 1)
#define UNIVERSAL_SERIAL_INTERFACE_CLOCK_STOP_ON    (1u << 2)

static UINT32 sDiagnosticControl;

static volatile UINT32 *UniversalSerialInterfaceRegister(UINT32 Offset) {
    return (volatile UINT32 *)(UINTN)(UNIVERSAL_SERIAL_INTERFACE_PHYSICAL +
                                      (UINT64)Offset);
}

UINT32 UsbHostHighSpeedInterIntegratedCircuitDiagnosticUniversalSerial(
    void) {
    return sDiagnosticControl;
}

void UsbHostUniversalSerialInterfaceEnableInterIntegratedCircuit(void) {
    UINT32 Control;
    UINT32 Option;
    UINTN Index;

    Control = *UniversalSerialInterfaceRegister(UNIVERSAL_SERIAL_INTERFACE_CONTROL);
    Control &= ~UNIVERSAL_SERIAL_INTERFACE_RESET_BIT;
    *UniversalSerialInterfaceRegister(UNIVERSAL_SERIAL_INTERFACE_CONTROL) =
        Control;
    for (Index = 0; Index < 1000u; Index++) {
        __asm__ volatile("yield" ::: "memory");
    }

    Option = *UniversalSerialInterfaceRegister(UNIVERSAL_SERIAL_INTERFACE_OPTION);
    Option &= ~UNIVERSAL_SERIAL_INTERFACE_CLOCK_STOP_ON;
    Option |= UNIVERSAL_SERIAL_INTERFACE_CLOCK_REQUEST_ON;
    *UniversalSerialInterfaceRegister(UNIVERSAL_SERIAL_INTERFACE_OPTION) =
        Option;

    sDiagnosticControl =
        *UniversalSerialInterfaceRegister(UNIVERSAL_SERIAL_INTERFACE_CONTROL);
}

#else

UINT32 UsbHostHighSpeedInterIntegratedCircuitDiagnosticUniversalSerial(
    void) {
    return 0;
}

void UsbHostUniversalSerialInterfaceEnableInterIntegratedCircuit(void) {
}

#endif
