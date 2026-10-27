/*
 * Utility.c — SDHCI 寄存器/延时（bd-pi-sd）
 */
#include "Sdhci/Private.h"

volatile UINT32 *SdhciRegister32(UINT32 Offset) {
    return (volatile UINT32 *)(UINTN)(SDHCI_BASE + Offset);
}

UINT64 gSdhciCounterFrequency;
UINT64 gSdhciLastWriteMicroseconds;
UINT32 gSdhciTwoTicksMicroseconds = 6;
UINT32 gSdhciBaseClockHertz = 100000000u;
UINT32 gSdhciRelativeCardAddress;
int gSdhciHighCapacity;
int gSdhciReady;
UINT32 gSdhciResponse[4];

UINT64 SdhciCounterNow(void) {
    UINT64 Value;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(Value));
    return Value;
}

void SdhciDelayMicroseconds(UINT32 Microseconds) {
    UINT64 Start;
    UINT64 Need;

    if (gSdhciCounterFrequency == 0) {
        __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(gSdhciCounterFrequency));
        if (gSdhciCounterFrequency == 0) {
            gSdhciCounterFrequency = 19200000ULL;
        }
    }
    Start = SdhciCounterNow();
    Need = (gSdhciCounterFrequency * (UINT64)Microseconds) / 1000000ULL;
    if (Need == 0) {
        Need = 1;
    }
    while ((SdhciCounterNow() - Start) < Need) {
        HalCpuRelax();
    }
}

UINT64 SdhciNowMicroseconds(void) {
    if (gSdhciCounterFrequency == 0) {
        SdhciDelayMicroseconds(0);
    }
    return (SdhciCounterNow() * 1000000ULL) / gSdhciCounterFrequency;
}

/* Arasan：非 BUFFER 连续写须隔 ≥2 个 SD 时钟（U-Boot twoticks_delay） */
void SdhciWrite32(UINT32 Offset, UINT32 Value) {
    if (Offset != SDHCI_BUFFER) {
        while ((SdhciNowMicroseconds() - gSdhciLastWriteMicroseconds) < (UINT64)gSdhciTwoTicksMicroseconds) {
            HalCpuRelax();
        }
    }
    *SdhciRegister32(Offset) = Value;
    gSdhciLastWriteMicroseconds = SdhciNowMicroseconds();
}

UINT32 SdhciRead32(UINT32 Offset) {
    /* 仅 4 字节对齐；外设窗非对齐 ldr 会挂（录像卡在 SdhciSoftwareReset） */
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

void SdhciWrite8(UINT32 Offset, UINT8 Value) {
    UINT32 Aligned = Offset & ~3u;
    UINT32 Shift = (Offset & 3u) * 8u;
    UINT32 Old = SdhciRead32(Aligned);
    UINT32 Mask = 0xFFu << Shift;
    SdhciWrite32(Aligned, (Old & ~Mask) | (((UINT32)Value << Shift) & Mask));
}

/* 16 位寄存器：只改半字，勿 SdhciWrite32 钟控时清掉 timeout@0x2E */
void SdhciWrite16(UINT32 Offset, UINT16 Value) {
    UINT32 Aligned = Offset & ~3u;
    UINT32 Shift = (Offset & 2u) ? 16u : 0u;
    UINT32 Old = SdhciRead32(Aligned);
    UINT32 Mask = 0xFFFFu << Shift;
    SdhciWrite32(Aligned, (Old & ~Mask) | (((UINT32)Value << Shift) & Mask));
}

/* 墙钟等待（SPIN_MAXIMUM 在 A53 上只有数 ms，CMD 易假超时 st=0） */
int SdhciWaitMillisecondsMask32(UINT32 Offset, UINT32 Mask, int Set, UINT32 Milliseconds) {
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

int SdhciWaitMillisecondsMask8(UINT32 Offset, UINT8 Mask, int Set, UINT32 Milliseconds) {
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

void SdhciWrite16Pair(UINT32 OffsetLow, UINT16 LowHalf, UINT16 HighHalf) {
    /* TRANSFER_MODE@0x0C + COMMAND@0x0E 一次 32 位提交（bcm2835 shadow） */
    SdhciWrite32(OffsetLow & ~3u, ((UINT32)HighHalf << 16) | (UINT32)LowHalf);
}

