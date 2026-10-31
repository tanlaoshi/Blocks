/*
 * HalSerial.c — RISC-V UART16550 / DW-APB（基址与间距见 BoardConfig.h）
 *
 * SERIAL_ENABLE=0：不碰 UART；分模块 quiet 见 SerialConfig.h。
 */
#include "HalSerial.h"
#include "Text.h"
#include "BoardConfig.h"
#include "SerialConfig.h"

#ifndef BOARD_UART_REG_SHIFT
#define BOARD_UART_REG_SHIFT 0
#endif

#define UART_BASE  ((UINTN)BOARD_UART_BASE)
#define UART_OFF(N) ((UINTN)(N) << (BOARD_UART_REG_SHIFT))
#define UART_THR   (*(volatile UINT8 *)(UART_BASE + UART_OFF(0)))
#define UART_LSR   (*(volatile UINT8 *)(UART_BASE + UART_OFF(5)))
#define UART_LSR_THRE  (1u << 5)
#define UART_LSR_TEMT  (1u << 6)
#define UART_LSR_DR    (1u << 0)

static int gShellOwnUart;

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

static void UartPut(char C) {
#if SERIAL_ENABLE
    while ((UART_LSR & UART_LSR_THRE) == 0) {
    }
    UART_THR = (UINT8)C;
#else
    (void)C;
#endif
}

/* \\n → \\r\\n（含 Input/timer 等直呼 HalSerialWrite 的路径）；已有 \\r\\n 不叠 */
static void UartWriteRaw(const char *Text) {
#if !SERIAL_ENABLE
    (void)Text;
#else
    if (Text == 0) {
        return;
    }
    while (*Text) {
        if (*Text == '\r' && Text[1] == '\n') {
            UartPut('\r');
            UartPut('\n');
            Text += 2;
            continue;
        }
        if (*Text == '\n') {
            UartPut('\r');
            UartPut('\n');
            Text++;
            continue;
        }
        UartPut(*Text++);
    }
#endif
}

void HalSerialInitialize(void) {
}

void HalSerialRetryIfMissing(void) {
}

void HalSerialEnableRxIrq(void) {
}

int HalSerialPresent(void) {
#if SERIAL_ENABLE
    return 1;
#else
    return 0;
#endif
}

void HalSerialRxPump(void) {
}

void HalSerialGopEnable(void) {
}

void HalSerialBootFontApply(void) {
}

void HalSerialGopMirror(int Enable) {
    (void)Enable;
}

int HalSerialGopMirroring(void) {
    return 0;
}

const char *HalSerialLogText(void) {
    return "";
}

void HalSerialWriteChannel(int Channel, const char *Text) {
    if (!Text) {
        return;
    }
    /*
     * 对齐 Arm64：ShellOwn 只表示交互壳接管，不挡 Log / fault 上 UART。
     * 旧逻辑 Own 后直接 return → Duo run user 进 U 后缺页/wfi 串口全黑
     * （boot 自测在 Own 前，故看不见此坑）。
     */
    if (ChannelUartOn(Channel)) {
        UartWriteRaw(Text);
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
    return 0;
}

void HalSerialRuntimeMarkSaved(void) {
}

UINTN HalSerialRuntimeSnapshot(char *Dst, UINTN Max) {
    (void)Dst;
    (void)Max;
    return 0;
}

UINTN HalSerialBootSnapshot(char *Dst, UINTN Max) {
    (void)Dst;
    (void)Max;
    return 0;
}

void HalSerialReplayUsb(void) {
}

void HalSerialBootMarkForce(const char *Text) {
    (void)Text;
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

int HalSerialDataReady(void) {
#if !SERIAL_ENABLE
    return 0;
#else
    return (UART_LSR & UART_LSR_DR) ? 1 : 0;
#endif
}

char HalSerialReadChar(void) {
#if !SERIAL_ENABLE
    return 0;
#else
    while (!HalSerialDataReady()) {
    }
    return (char)UART_THR;
#endif
}

void HalSerialBootLogRewind(void) {
}

void HalSerialGopMute(int Mute) {
    (void)Mute;
}

void HalSerialBootMarkChannel(int Channel, const char *Text) {
    HalSerialWriteChannel(Channel, Text);
}

void HalSerialBootMark(const char *Text) {
    HalSerialBootMarkChannel(SLOG_BOOT, Text);
}

void HalSerialBootHoldMs(UINT32 Ms) {
    (void)Ms;
}

void HalSerialFormatHex(char *Buf, UINT64 Value, int Digits) {
    if (Buf == 0 || Digits <= 0 || Digits > 16) {
        return;
    }
    FormatHexadecimal(Buf, Digits + 1, Value, Digits, 0, 0);
}

void HalSerialPutByte(UINT8 Value) {
    UartPut((char)Value);
}

int HalSerialTryGetByte(UINT8 *Out) {
#if !SERIAL_ENABLE
    (void)Out;
    return 0;
#else
    if (Out == 0 || (UART_LSR & UART_LSR_DR) == 0) {
        return 0;
    }
    *Out = UART_THR;
    return 1;
#endif
}

void HalSerialDrainTransmitter(void) {
#if SERIAL_ENABLE
    UINT32 Guard = 0;

    while ((UART_LSR & UART_LSR_TEMT) == 0 && Guard < 200000u) {
        __asm__ volatile("" ::: "memory");
        Guard++;
    }
#else
    (void)0;
#endif
}
