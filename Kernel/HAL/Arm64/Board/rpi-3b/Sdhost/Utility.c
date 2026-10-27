/*
 * Utility.c — SDHOST 寄存器/延时/转储（bd-pi-sd）
 */
#include "Sdhost/Private.h"

volatile UINT32 *SdhostRegister32(UINT32 Offset) {
    return (volatile UINT32 *)(UINTN)(SDHOST_BASE + Offset);
}

void SdhostWrite32(UINT32 Offset, UINT32 Value) {
    *SdhostRegister32(Offset) = Value;
}

UINT32 SdhostRead32(UINT32 Offset) {
    return *SdhostRegister32(Offset);
}

UINT64 SdhostCounterNow(void) {
    UINT64 Value;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(Value));
    return Value;
}

void SdhostDelayMicroseconds(UINT32 Microseconds) {
    UINT64 Start;
    UINT64 Need;

    if (gSdhostCounterFrequency == 0) {
        __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(gSdhostCounterFrequency));
        if (gSdhostCounterFrequency == 0) {
            gSdhostCounterFrequency = 19200000ULL;
        }
    }
    Start = SdhostCounterNow();
    Need = (gSdhostCounterFrequency * (UINT64)Microseconds) / 1000000ULL;
    if (Need == 0) {
        Need = 1;
    }
    while ((SdhostCounterNow() - Start) < Need) {
        HalCpuRelax();
    }
}

void SdhostDelayMilliseconds(UINT32 Milliseconds) {
    SdhostDelayMicroseconds(Milliseconds * 1000u);
}

void SdhostMarkHexadecimal32(const char *Tag, UINT32 Value) {
    char Buffer[9];
    HalSerialBootMark(Tag);
    HalSerialFormatHex(Buffer, Value, 8);
    HalSerialBootMark(Buffer);
}

void SdhostDumpRegisters(const char *Reason) {
    HalSerialBootMark(Reason);
    SdhostMarkHexadecimal32(" SDCMD=", SdhostRead32(SDCMD));
    SdhostMarkHexadecimal32(" SDHSTS=", SdhostRead32(SDHSTS));
    SdhostMarkHexadecimal32(" SDCDIV=", SdhostRead32(SDCDIV));
    SdhostMarkHexadecimal32(" SDEDM=", SdhostRead32(SDEDM));
    HalSerialBootMark("\n");
}

