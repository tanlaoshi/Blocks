/*
 * UsbHost/Utility.c — MIX 2 USB bringup：字串 / 延时 / 缓存 / MMIO
 */
#include "UsbHost/Private.h"

#ifndef BOARD_MIX2_USB_HOST_BRINGUP
#define BOARD_MIX2_USB_HOST_BRINGUP 0
#endif

#if BOARD_MIX2_USB_HOST_BRINGUP

void UsbHostFormatHexadecimal32(char *Out, UINT32 Value) {
    static const char Digits[] = "0123456789abcdef";
    int Index;

    Out[0] = '0';
    Out[1] = 'x';
    for (Index = 0; Index < 8; Index++) {
        Out[2 + Index] = Digits[(Value >> (28 - Index * 4)) & 0xFu];
    }
    Out[10] = '\0';
}

void UsbHostCopyText(char *Destination, const char *Source, UINTN Capacity) {
    UINTN Index;

    for (Index = 0; Index + 1u < Capacity && Source[Index] != '\0'; Index++) {
        Destination[Index] = Source[Index];
    }
    Destination[Index] = '\0';
}

void UsbHostAppendText(char *Destination, const char *Source, UINTN Capacity) {
    UINTN Length;

    for (Length = 0; Length < Capacity && Destination[Length] != '\0'; Length++) {
    }
    UsbHostCopyText(Destination + Length, Source, Capacity - Length);
}

void UsbHostStallYield(UINTN Iterations) {
    UINTN Index;

    for (Index = 0; Index < Iterations; Index++) {
        __asm__ volatile("yield" ::: "memory");
    }
}

void UsbHostStallYieldPumpPower(UINTN Iterations) {
    static UINTN sYieldsSincePowerPoll;
    UINTN Index;
    const UINTN PowerPollEvery = 20000u;

    /*
     * 按累计 yield 泵键：复位紧循环里 Stall(50) 也会采到短按，
     * 又不会每 50 次就打一次 SPMI（会拖死 chirp / hub）。
     */
    for (Index = 0; Index < Iterations; Index++) {
        __asm__ volatile("yield" ::: "memory");
        sYieldsSincePowerPoll++;
        if (sYieldsSincePowerPoll >= PowerPollEvery) {
            sYieldsSincePowerPoll = 0;
            (void)BoardPowerButtonPressed();
        }
    }
}

void UsbHostCacheCleanRange(void *Address, UINTN Size) {
    UINT64 Cursor = (UINT64)(UINTN)Address & ~63ULL;
    UINT64 End = (UINT64)(UINTN)Address + Size;

    for (; Cursor < End; Cursor += 64u) {
        __asm__ volatile("dc cvac, %0" ::"r"(Cursor) : "memory");
    }
    __asm__ volatile("dsb ish" ::: "memory");
}

void UsbHostCacheCleanInvalidateRange(void *Address, UINTN Size) {
    UINT64 Cursor = (UINT64)(UINTN)Address & ~63ULL;
    UINT64 End = (UINT64)(UINTN)Address + Size;

    for (; Cursor < End; Cursor += 64u) {
        __asm__ volatile("dc civac, %0" ::"r"(Cursor) : "memory");
    }
    __asm__ volatile("dsb ish" ::: "memory");
}

volatile UINT32 *UsbHostMmio32(UINT64 Address) {
    return (volatile UINT32 *)(UINTN)Address;
}

UINT32 UsbHostReadMmio32(UINT64 Address) {
    return *UsbHostMmio32(Address);
}

void UsbHostWriteMmio32(UINT64 Address, UINT32 Value) {
    *UsbHostMmio32(Address) = Value;
}

UINT64 UsbHostReadMmio64(UINT64 Address) {
    return ((UINT64)UsbHostReadMmio32(Address + 4u) << 32) | UsbHostReadMmio32(Address);
}

void UsbHostWriteMmio64(UINT64 Address, UINT64 Value) {
    UsbHostWriteMmio32(Address, (UINT32)Value);
    UsbHostWriteMmio32(Address + 4u, (UINT32)(Value >> 32));
}

#else

void UsbHostFormatHexadecimal32(char *Out, UINT32 Value) {
    (void)Out;
    (void)Value;
}

void UsbHostCopyText(char *Destination, const char *Source, UINTN Capacity) {
    (void)Destination;
    (void)Source;
    (void)Capacity;
}

void UsbHostAppendText(char *Destination, const char *Source, UINTN Capacity) {
    (void)Destination;
    (void)Source;
    (void)Capacity;
}

void UsbHostStallYield(UINTN Iterations) {
    (void)Iterations;
}

void UsbHostStallYieldPumpPower(UINTN Iterations) {
    (void)Iterations;
}

void UsbHostCacheCleanRange(void *Address, UINTN Size) {
    (void)Address;
    (void)Size;
}

void UsbHostCacheCleanInvalidateRange(void *Address, UINTN Size) {
    (void)Address;
    (void)Size;
}

volatile UINT32 *UsbHostMmio32(UINT64 Address) {
    (void)Address;
    return 0;
}

UINT32 UsbHostReadMmio32(UINT64 Address) {
    (void)Address;
    return 0;
}

void UsbHostWriteMmio32(UINT64 Address, UINT32 Value) {
    (void)Address;
    (void)Value;
}

UINT64 UsbHostReadMmio64(UINT64 Address) {
    (void)Address;
    return 0;
}

void UsbHostWriteMmio64(UINT64 Address, UINT64 Value) {
    (void)Address;
    (void)Value;
}

#endif
