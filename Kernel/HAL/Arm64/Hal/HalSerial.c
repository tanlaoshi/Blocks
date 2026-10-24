/*
 * HalSerial.c — aarch64 串口（基址/类型见 BoardConfig.h / PR-B2）
 *
 * - virt：PL011
 * - Pi3BPlus 等 BOARD_UART_MINI=1：BCM AUX mini-UART（寄存器布局不同）
 *
 * SERIAL_ENABLE=0：不碰 UART；分模块 quiet 见 SerialConfig.h。
 */
#include "HalSerial.h"
#include "Text.h"
#include "BoardConfig.h"
#include "SerialConfig.h"

#ifndef BOARD_UART_MINI
#define BOARD_UART_MINI 0
#endif

#define UART_BASE  ((UINTN)BOARD_UART_BASE)

#if BOARD_UART_MINI
/* BCM2835 ARM Peripherals §2.2 AUX mini UART；BASE = AUX 块 */
#define MU_IO     (*(volatile UINT32 *)(UART_BASE + 0x40))
#define MU_LSR    (*(volatile UINT32 *)(UART_BASE + 0x54))
#define MU_ENABLES (*(volatile UINT32 *)(UART_BASE + 0x04))
#define MU_LSR_TX_EMPTY   (1u << 5)
#define MU_LSR_DATA_READY (1u << 0)
#else
#define PL011_DR    (*(volatile UINT32 *)(UART_BASE + 0x00))
#define PL011_FR    (*(volatile UINT32 *)(UART_BASE + 0x18))
#define PL011_FR_TXFF  (1u << 5)
#define PL011_FR_RXFE  (1u << 4)
#endif

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
#if BOARD_UART_MINI
    while ((MU_LSR & MU_LSR_TX_EMPTY) == 0) {
    }
    MU_IO = (UINT32)(UINT8)C;
#else
    while (PL011_FR & PL011_FR_TXFF) {
    }
    PL011_DR = (UINT32)(UINT8)C;
#endif
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
#if SERIAL_ENABLE && BOARD_UART_MINI
    /* 固件/U-Boot 已配好波特率；只确保 AUX mini-UART 使能位，不改 BAUD */
    MU_ENABLES |= 1u;
#else
    /* QEMU virt 已初始化 PL011；SERIAL=0 时也不访问 */
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
#elif BOARD_UART_MINI
    return (MU_LSR & MU_LSR_DATA_READY) ? 1 : 0;
#else
    return (PL011_FR & PL011_FR_RXFE) ? 0 : 1;
#endif
}

char HalSerialReadChar(void) {
#if !SERIAL_ENABLE
    return 0;
#elif BOARD_UART_MINI
    while (!HalSerialDataReady()) {
    }
    return (char)(MU_IO & 0xFFu);
#else
    while (!HalSerialDataReady()) {
    }
    return (char)(PL011_DR & 0xFFu);
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
    if (Buf == 0 || Digits <= 0 || Digits > 16) {
        return;
    }
    FormatHexadecimal(Buf, Digits + 1, Value, Digits, 0, 0);
}
