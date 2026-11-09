/*
 * HalSerialWrite.c — 通道过滤与写出（PR-NORM-3：对齐 X64 HalSerialWrite）
 */
#include "HalSerial.h"
#include "HalSerialPrivate.h"
#include "BoardConfig.h"
#include "SerialConfig.h"

#ifndef BOARD_UART_MINI
#define BOARD_UART_MINI 0
#endif

static int gShellOwnUart;
/* 对齐 X64：ShellOwn 后 Log* → 缓冲 → RUNTIME*.LOG，不再占 UART */
#define RT_LOG_MAX 16384u
static char gRtLog[RT_LOG_MAX];
static UINTN gRtLen;
static int gRtDirty;
static char gBootSnap[GOP_RING];
static UINTN gBootSnapLen;

void HalSerialBootCapture(const char *Text) {
    while (Text && *Text) {
        if (gBootSnapLen + 1 >= sizeof(gBootSnap)) {
            break;
        }
        gBootSnap[gBootSnapLen++] = *Text++;
    }
}

static void RtAppend(const char *Text) {
    if (!gShellOwnUart || !Text) {
        return;
    }
    while (*Text) {
        if (gRtLen + 1 >= RT_LOG_MAX) {
            UINTN Keep = RT_LOG_MAX / 2u;
            UINTN i;
            for (i = 0; i < Keep; i++) {
                gRtLog[i] = gRtLog[gRtLen - Keep + i];
            }
            gRtLen = Keep;
        }
        gRtLog[gRtLen++] = *Text++;
        gRtDirty = 1;
    }
}

static int ChannelUartOn(int Channel) {
#if !SERIAL_ENABLE
    (void)Channel;
    return 0;
#else
    switch (Channel) {
    case SLOG_BOOT: return SERIAL_BOOT;
    case SLOG_USB:  return SERIAL_USB;
    case SLOG_SMP:  return SERIAL_SMP;
    case SLOG_GUI:  return SERIAL_GUI;
    case SLOG_NET:  return SERIAL_NET;
    case SLOG_FS:   return SERIAL_FS;
    case SLOG_MEM:  return SERIAL_MEM;
    case SLOG_DRV:  return SERIAL_DRV;
    case SLOG_MISC:
    default:            return SERIAL_MISC;
    }
#endif
}

static int ChannelGopOn(int Channel) {
#if !SCREEN_LOG
    (void)Channel;
    return 0;
#else
    switch (Channel) {
    case SLOG_BOOT: return SCREEN_LOG_BOOT;
    case SLOG_USB:  return SCREEN_LOG_USB;
    case SLOG_SMP:  return SCREEN_LOG_SMP;
    case SLOG_GUI:  return SCREEN_LOG_GUI;
    case SLOG_NET:  return SCREEN_LOG_NET;
    case SLOG_FS:   return SCREEN_LOG_FS;
    case SLOG_MEM:  return SCREEN_LOG_MEM;
    case SLOG_DRV:  return SCREEN_LOG_DRV;
    case SLOG_MISC:
    default:            return SCREEN_LOG_MISC;
    }
#endif
}

static void GopMirrorLine(const char *Text) {
    if (!Text || !gGopMirror || !gVideoUp || gGopMute) {
        return;
    }
    GopWrite(Text);
}

static void BootHoldSpinMilliseconds(UINT32 Ms) {
    UINT32 m;
    for (m = 0; m < Ms; m++) {
        volatile UINT32 i;
        for (i = 0; i < 80000u; i++) {
            __asm__ volatile("nop");
        }
    }
}

void HalSerialBootHoldMilliseconds(UINT32 Ms) {
    if (Ms == 0) {
        return;
    }
    HalSerialBootMark("Boot: HOLD — CoolTerm expect: U-Boot then * + Blocks uart\n");
    HalSerialBootMark("Boot: HOLD — if U-Boot quiet: check GPIO14->TTL RX\n");
#if SERIAL_ENABLE && BOARD_UART_MINI
    {
        char Hex[9];
        UINTN SnapshotLength;
        char Snapshot[512];
        UINTN Index;
        volatile UINT32 *MiniUartControl =
            (volatile UINT32 *)((UINTN)BOARD_UART_BASE + 0x60);
        volatile UINT32 *MiniUartLineStatus =
            (volatile UINT32 *)((UINTN)BOARD_UART_BASE + 0x54);
        volatile UINT32 *MiniUartLineControl =
            (volatile UINT32 *)((UINTN)BOARD_UART_BASE + 0x4C);

        HalSerialInitialize();
        HalSerialBootMark("Boot: uart GPFSEL1=");
        HalSerialFormatHex(Hex, *(volatile UINT32 *)(UINTN)0x3F200004ULL, 8);
        HalSerialBootMark(Hex);
        HalSerialBootMark(" CNTL=");
        HalSerialFormatHex(Hex, *MiniUartControl, 8);
        HalSerialBootMark(Hex);
        HalSerialBootMark(" LSR=");
        HalSerialFormatHex(Hex, *MiniUartLineStatus, 8);
        HalSerialBootMark(Hex);
        HalSerialBootMark(" LCR=");
        HalSerialFormatHex(Hex, *MiniUartLineControl, 8);
        HalSerialBootMark(Hex);
        HalSerialBootMark("\n");
        /* 慢打；仍无 UART-HOLD → TX 线/口（halt 若来自 HDMI 壳则未证明 RX） */
        UartWriteRaw("UART-HOLD\r\n");
        SnapshotLength = HalSerialBootSnapshot(Snapshot, sizeof(Snapshot));
        for (Index = 0; Index < SnapshotLength; Index++) {
            char One[2];

            One[0] = Snapshot[Index];
            One[1] = 0;
            UartWriteRaw(One);
            if (Snapshot[Index] == '\n') {
                BootHoldSpinMilliseconds(15);
            }
        }
    }
#endif
    BootHoldSpinMilliseconds(Ms);
    HalSerialBootMark("Boot: HOLD end\n");
}

void HalSerialWriteChannel(int Channel, const char *Text) {
    if (!Text) {
        return;
    }
    RingAppend(Text);
    RtAppend(Text);
    /*
     * 对齐 X64：ShellOwn 后 UART 只留给 WriteShell（blocks> / 回显）；
     * Log* 进 ring + RUNTIME 缓冲，Worker 末 RuntimeLogFlush 落盘。
     */
    if (!gShellOwnUart && ChannelUartOn(Channel)) {
        UartWriteRaw(Text);
    }
    if (!gShellOwnUart && ChannelGopOn(Channel)) {
        GopMirrorLine(Text);
    }
}

void HalSerialShellOwn(void) {
    gShellOwnUart = 1;
}

int HalSerialShellOwned(void) {
    return gShellOwnUart;
}

void HalSerialWriteShell(const char *Text) {
    UartWriteRaw(Text);
}

int HalSerialRuntimeDirty(void) {
    return gRtDirty;
}

void HalSerialRuntimeMarkSaved(void) {
    gRtDirty = 0;
}

UINTN HalSerialRuntimeSnapshot(char *Dst, UINTN Max) {
    UINTN N;
    UINTN i;

    if (!Dst || Max == 0) {
        return 0;
    }
    N = gRtLen;
    if (N >= Max) {
        N = Max - 1;
    }
    for (i = 0; i < N; i++) {
        Dst[i] = gRtLog[i];
    }
    Dst[N] = 0;
    return N;
}

UINTN HalSerialBootSnapshot(char *Dst, UINTN Max) {
    UINTN N;
    UINTN i;

    if (!Dst || Max == 0) {
        return 0;
    }
    N = gBootSnapLen;
    if (N >= Max) {
        N = Max - 1;
    }
    for (i = 0; i < N; i++) {
        Dst[i] = gBootSnap[i];
    }
    Dst[N] = 0;
    return N;
}

void HalSerialReplayUsb(void) {
}

void HalSerialBootMarkForce(const char *Text) {
    if (!Text) {
        return;
    }
    HalSerialBootMarkChannel(SLOG_USB, Text);
}

void HalSerialWrite(const char *Text) {
    HalSerialWriteChannel(SLOG_MISC, Text);
}

void HalSerialWriteChannelHex32(int Channel, UINT32 Value) {
    char Buf[12];

    HalSerialFormatHex(Buf, Value, 8);
    HalSerialWriteChannel(Channel, Buf);
}

void HalSerialWriteChannelHex64(int Channel, UINT64 Value) {
    char Buf[20];

    HalSerialFormatHex(Buf, Value, 16);
    HalSerialWriteChannel(Channel, Buf);
}

void HalSerialBootMarkChannel(int Channel, const char *Text) {
    if (!Text) {
        return;
    }
    RingAppend(Text);
    RtAppend(Text);
    /* 对齐 X64：ShellOwn 后不再占 UART；屏仍认 Mirror */
    if (!gShellOwnUart && ChannelUartOn(Channel)) {
        UartWriteRaw(Text);
    }
    /* Mute 期间也上屏（对齐 X64 BootMark）；Mirror=0 后停 */
    if (ChannelGopOn(Channel) && gGopMirror && gVideoUp) {
        GopWrite(Text);
    }
}

void HalSerialBootMark(const char *Text) {
    HalSerialBootMarkChannel(SLOG_BOOT, Text);
}
