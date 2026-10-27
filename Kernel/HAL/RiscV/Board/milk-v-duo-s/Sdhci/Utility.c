/*
 * Utility.c — Duo SDHCI 寄存器/延时/标记（bd-duo-sd）
 */
#include "Sdhci/Private.h"

volatile UINT32 *SdhciRegister32(UINT32 Offset) {
    return (volatile UINT32 *)(UINTN)(SDHCI_BASE + Offset);
}

UINT64 SdhciReadTime(void) {
    UINT64 Value;
    __asm__ volatile("rdtime %0" : "=r"(Value));
    return Value;
}

void SdhciDelayMicroseconds(UINT32 Microseconds) {
    UINT64 Start = SdhciReadTime();
    UINT64 Need = ((UINT64)BOARD_TIMEBASE_HZ * (UINT64)Microseconds) / 1000000ULL;
    if (Need == 0) {
        Need = 1;
    }
    while ((SdhciReadTime() - Start) < Need) {
        HalCpuRelax();
    }
}

UINT32 SdhciRead32(UINT32 Offset) {
    return *SdhciRegister32(Offset & ~3u);
}

UINT8 SdhciRead8(UINT32 Offset) {
    UINT32 Shift = (Offset & 3u) * 8u;
    return (UINT8)((SdhciRead32(Offset) >> Shift) & 0xFFu);
}

UINT16 SdhciRead16(UINT32 Offset) {
    UINT32 Shift = (Offset & 2u) ? 16u : 0u;
    return (UINT16)((SdhciRead32(Offset) >> Shift) & 0xFFFFu);
}

void SdhciWrite32(UINT32 Offset, UINT32 Value) {
    *SdhciRegister32(Offset & ~3u) = Value;
}

void SdhciWrite8(UINT32 Offset, UINT8 Value) {
    UINT32 Aligned = Offset & ~3u;
    UINT32 Shift = (Offset & 3u) * 8u;
    UINT32 Old = SdhciRead32(Aligned);
    UINT32 Mask = 0xFFu << Shift;
    SdhciWrite32(Aligned, (Old & ~Mask) | ((UINT32)Value << Shift));
}

void SdhciWrite16(UINT32 Offset, UINT16 Value) {
    UINT32 Aligned = Offset & ~3u;
    UINT32 Shift = (Offset & 2u) ? 16u : 0u;
    UINT32 Old = SdhciRead32(Aligned);
    UINT32 Mask = 0xFFFFu << Shift;
    SdhciWrite32(Aligned, (Old & ~Mask) | ((UINT32)Value << Shift));
}

void SdhciMark(const char *Text) {
    HalSerialBootMark(Text);
}

void SdhciMarkHexadecimal32(const char *Tag, UINT32 Value) {
    char Buffer[9];
    HalSerialBootMark(Tag);
    HalSerialFormatHex(Buffer, Value, 8);
    HalSerialBootMark(Buffer);
}

int SdhciWaitMilliseconds8(UINT32 Offset, UINT8 Mask, int Set, UINT32 Milliseconds) {
    UINT32 n;
    for (n = 0; n < Milliseconds * 10u; n++) {
        UINT8 Value = SdhciRead8(Offset);
        if (Set) {
            if ((Value & Mask) != 0) {
                return 0;
            }
        } else if ((Value & Mask) == 0) {
            return 0;
        }
        SdhciDelayMicroseconds(100);
    }
    return -1;
}

int SdhciWaitMilliseconds32(UINT32 Offset, UINT32 Mask, int Set, UINT32 Milliseconds) {
    UINT32 n;
    for (n = 0; n < Milliseconds * 10u; n++) {
        UINT32 Value = SdhciRead32(Offset);
        if (Set) {
            if ((Value & Mask) != 0) {
                return 0;
            }
        } else if ((Value & Mask) == 0) {
            return 0;
        }
        SdhciDelayMicroseconds(100);
    }
    return -1;
}
