/*
 * HalSerial.c — X64：COM1 上的 16550 UART（早期接棒日志）
 *
 * 【初学者】
 * - Hal/X64：Boot 与 Kernel 共用 COM1 115200；门面 HalSerial.h。
 * - 入口：HalSerialInitialize / HalSerialWrite / HalSerialWriteChannel。
 * - 边界：SERIAL_ENABLE=0 编译期关；SCREEN_LOG 走 HalSerialGop。
 */
#include "HalSerial.h"
#include "HalSerialGop.h"
#include "SerialConfig.h"

#define COM1 0x3F8u
/* 宿主机 stdio 一堵，长等会拖死整机（鼠/GUI 假死）。短等后丢字节。 */
#define SERIAL_WAIT 2048

static int gSerialOk;
static int gSerialReady;
static int gShellOwnUart;

static inline void Out8(UINT16 Port, UINT8 Value) {
    __asm__ volatile("outb %0, %1" : : "a"(Value), "Nd"(Port));
}

static inline UINT8 In8(UINT16 Port) {
    UINT8 Value;
    __asm__ volatile("inb %1, %0" : "=a"(Value) : "Nd"(Port));
    return Value;
}

static int ProbeCom1(void) {
    UINT8 A;
    UINT8 B;

#if !SERIAL_ENABLE
    return 0;
#else
    Out8(COM1 + 7, 0x55);
    A = In8(COM1 + 7);
    Out8(COM1 + 7, 0xAA);
    B = In8(COM1 + 7);
    return (A == 0x55 && B == 0xAA) ? 1 : 0;
#endif
}

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
    int Timeout;

    if (!gSerialOk) {
        return;
    }
    /* THR 不空：短等；仍满则丢弃（勿堵鼠标热路径） */
    Timeout = SERIAL_WAIT;
    while (Timeout-- && !(In8(COM1 + 5) & 0x20u)) {
        __asm__ volatile("pause");
    }
    if (!(In8(COM1 + 5) & 0x20u)) {
        return;
    }
    Out8(COM1, (UINT8)C);
#else
    (void)C;
#endif
}

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
#if !SERIAL_ENABLE
    gSerialOk = 0;
    gSerialReady = 1;
    return;
#else
    if (gSerialReady) {
        return;
    }
    gSerialOk = ProbeCom1();
    if (gSerialOk) {
        Out8(COM1 + 1, 0x00);
        Out8(COM1 + 3, 0x80);
        Out8(COM1 + 0, 0x01); /* 115200 */
        Out8(COM1 + 1, 0x00);
        Out8(COM1 + 3, 0x03); /* 8N1 */
        Out8(COM1 + 2, 0xC7);
        Out8(COM1 + 4, 0x0B);
        UartWriteRaw("Blocks X64\n");
        UartWriteRaw("COM1 Serial OK\n");
    } else {
        /* 无 COM1：静默（Boot 阶段可能已报过） */
    }
    gSerialReady = 1;
#endif
}

void HalSerialRetryIfMissing(void) {
#if SERIAL_ENABLE
    if (!gSerialOk) {
        gSerialOk = ProbeCom1();
        if (gSerialOk && !gSerialReady) {
            HalSerialInitialize();
        }
    }
#endif
}

void HalSerialEnableRxIrq(void) {
}

int HalSerialPresent(void) {
    return gSerialOk;
}

void HalSerialRxPump(void) {
}

const char *HalSerialLogText(void) {
    return "";
}

void HalSerialWriteChannel(int Channel, const char *Text) {
    if (Text == 0) {
        return;
    }
    if (!gShellOwnUart && ChannelUartOn(Channel)) {
        UartWriteRaw(Text);
    }
    /* 屏：与 UART 独立；无 COM1 也可画（受 SCREEN_LOG_*） */
    HalSerialGopTryMirror(Channel, Text);
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
    return gSerialOk && (In8(COM1 + 5) & 0x01) ? 1 : 0;
#endif
}

char HalSerialReadChar(void) {
#if !SERIAL_ENABLE
    return 0;
#else
    while (!HalSerialDataReady()) {
        __asm__ volatile("pause");
    }
    return (char)In8(COM1);
#endif
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
