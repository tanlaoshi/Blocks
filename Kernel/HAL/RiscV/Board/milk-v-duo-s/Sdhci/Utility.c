/*
 * Utility.c — Duo SDHCI 寄存器/延时/标记（bd-duo-sd）
 *
 * PR-30 S1：全部访问都带 SDHCI_HOST*，SD0/SD1 共用同一份代码。
 */
#include "Sdhci/Private.h"

volatile UINT32 *SdhciRegister32(SDHCI_HOST *Host, UINT32 Offset) {
    return (volatile UINT32 *)(UINTN)(Host->Base + Offset);
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

void SdhciDelayMilliseconds(UINT32 Milliseconds) {
    while (Milliseconds > 0) {
        UINT32 Step = (Milliseconds > 10u) ? 10u : Milliseconds;
        SdhciDelayMicroseconds(Step * 1000u);
        Milliseconds -= Step;
    }
}

UINT32 SdhciRead32(SDHCI_HOST *Host, UINT32 Offset) {
    return *SdhciRegister32(Host, Offset & ~3u);
}

UINT8 SdhciRead8(SDHCI_HOST *Host, UINT32 Offset) {
    UINT32 Shift = (Offset & 3u) * 8u;
    return (UINT8)((SdhciRead32(Host, Offset) >> Shift) & 0xFFu);
}

UINT16 SdhciRead16(SDHCI_HOST *Host, UINT32 Offset) {
    UINT32 Shift = (Offset & 2u) ? 16u : 0u;
    return (UINT16)((SdhciRead32(Host, Offset) >> Shift) & 0xFFFFu);
}

void SdhciWrite32(SDHCI_HOST *Host, UINT32 Offset, UINT32 Value) {
    *SdhciRegister32(Host, Offset & ~3u) = Value;
}

void SdhciWrite8(SDHCI_HOST *Host, UINT32 Offset, UINT8 Value) {
    UINT32 Aligned = Offset & ~3u;
    UINT32 Shift = (Offset & 3u) * 8u;
    UINT32 Old = SdhciRead32(Host, Aligned);
    UINT32 Mask = 0xFFu << Shift;
    SdhciWrite32(Host, Aligned, (Old & ~Mask) | ((UINT32)Value << Shift));
}

void SdhciWrite16(SDHCI_HOST *Host, UINT32 Offset, UINT16 Value) {
    UINT32 Aligned = Offset & ~3u;
    UINT32 Shift = (Offset & 2u) ? 16u : 0u;
    UINT32 Old = SdhciRead32(Host, Aligned);
    UINT32 Mask = 0xFFFFu << Shift;
    SdhciWrite32(Host, Aligned, (Old & ~Mask) | ((UINT32)Value << Shift));
}

/*
 * 日志出口。
 *
 * 原先走 HalSerialBootMark() → SLOG_BOOT 通道；该通道只在编译期 SERIAL_BOOT=1
 * 时才会写 UART（HalSerial.c:29-41 ChannelUartOn）。真机上「驱动已 bound 但一行
 * 都看不到」时，第一个要排除的就是这个开关，所以这里改成**直写 UART**
 * （HalSerialWriteShell 不做通道过滤），保证日志一定出得来。
 *
 * shell 里重跑 probe 时，用 SdhciWifiMarkSink() 把出口换成 gConsole.Write，
 * 这样输出和提示符/回显走同一条路径。
 */
static void (*gSdhciMarkSink)(const char *Text);

void SdhciWifiMarkSink(void (*Sink)(const char *Text)) {
    gSdhciMarkSink = Sink;
}

void SdhciMark(const char *Text) {
    if (Text == 0) {
        return;
    }
    if (gSdhciMarkSink != 0) {
        gSdhciMarkSink(Text);
        return;
    }
    HalSerialWriteShell(Text);
}

void SdhciMarkHex(const char *Tag, UINT32 Value, int Digits) {
    char Buffer[17];
    if (Digits < 1 || Digits > 16) {
        Digits = 8;
    }
    HalSerialFormatHex(Buffer, Value, Digits);
    HalSerialBootMark(Tag);
    HalSerialBootMark(Buffer);
}

void SdhciMarkDecimal(const char *Tag, UINT32 Value) {
    char Buffer[11];
    int Pos = 10;
    Buffer[Pos] = 0;
    do {
        Buffer[--Pos] = (char)('0' + (char)(Value % 10u));
        Value /= 10u;
    } while (Value != 0 && Pos > 0);
    SdhciMark(Tag);
    SdhciMark(&Buffer[Pos]);
}

int SdhciWait8(SDHCI_HOST *Host, UINT32 Offset, UINT8 Mask, int Set, UINT32 Milliseconds) {
    UINT32 n;
    for (n = 0; n < Milliseconds * 10u; n++) {
        UINT8 Value = SdhciRead8(Host, Offset);
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

int SdhciWait32(SDHCI_HOST *Host, UINT32 Offset, UINT32 Mask, int Set, UINT32 Milliseconds) {
    UINT32 n;
    for (n = 0; n < Milliseconds * 10u; n++) {
        UINT32 Value = SdhciRead32(Host, Offset);
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
