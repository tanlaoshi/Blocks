/*
 * HalSerial.c — RiscV QEMU virt：16550 兼容 UART（MMIO）
 *
 * 【初学者】
 * 芯片仍是「16550 家族」寄存器布局，但挂在内存地址上（不是 x86 端口）。
 * THR=发送保持；LSR.THRE=可以写下一字节。基址 / 寄存器间距见 BoardConfig。
 *
 * SERIAL_ENABLE=0：不碰 UART。门面与 X64/Arm64 相同：HalSerialWrite()。
 */
#include "HalSerial.h"
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
#define UART_LSR_DR    (1u << 0)

static int gShellOwnUart;
static int gSerialReady;

static int ChannelUartOn(int Channel) {
#if !SERIAL_ENABLE
    (void)Channel;
    return 0;
#else
    switch (Channel) {
    case SLOG_BOOT: return SERIAL_CH_BOOT;
    case SLOG_USB:  return SERIAL_CH_USB;
    case SLOG_SMP:  return SERIAL_CH_SMP;
    case SLOG_GUI:  return SERIAL_CH_GUI;
    case SLOG_NET:  return SERIAL_CH_NET;
    case SLOG_FS:   return SERIAL_CH_FS;
    case SLOG_MEM:  return SERIAL_CH_MEM;
    case SLOG_DRV:  return SERIAL_CH_DRV;
    case SLOG_MISC:
    default:            return SERIAL_CH_MISC;
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
#if SERIAL_ENABLE
    if (gSerialReady) {
        return;
    }
    gSerialReady = 1;
    UartWriteRaw("Blocks RiscV\n");
    UartWriteRaw("UART16550 Serial OK\n");
#endif
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
    if (gShellOwnUart) {
        return;
    }
    if (!ChannelUartOn(Channel)) {
        return;
    }
    UartWriteRaw(Text);
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

void HalSerialFormatHex(char *Buf, UINT64 Value, int Digits) {
    static const char Hex[] = "0123456789abcdef";
    int i;

    if (Buf == 0 || Digits <= 0 || Digits > 16) {
        return;
    }
    for (i = Digits - 1; i >= 0; i--) {
        Buf[i] = Hex[Value & 0xFu];
        Value >>= 4;
    }
    Buf[Digits] = 0;
}
