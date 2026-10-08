/*
 * HalSerial.c — X64：COM1 上的 16550 UART（早期接棒日志）
 *
 * 【初学者 · 什么是 COM1？】
 * PC 兼容机上经典串口，IO 端口基址 0x3F8。用 inb/outb 读写寄存器：
 *   +0 数据；+5 状态（bit5=发送保持寄存器空，才能写下一大字节）。
 * QEMU 常把客人 COM1 接到终端，于是 HalSerialWrite("hi\n") 能在宿主机看见。
 *
 * 与 Boot/BootPkg/BootSerial 同口同波特率（115200 8N1），方便整段 boot 日志连贯。
 *
 * TOY_SERIAL=0：编译期关掉，不 Probe、不碰端口（见 ToySerialConfig.h）。
 * 屏上滚动 / ring 缓冲相关 API 可为空操作。
 *
 * 【积木】门面 HalSerial.h；本文件是 X64 后端，可换成别的 UART。
 */
#include "HalSerial.h"
#include "ToySerialConfig.h"

#define COM1 0x3F8u
#define SERIAL_WAIT 1000000

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

static void WaitBit(UINT8 Mask) {
    int Timeout = SERIAL_WAIT;

    while (Timeout-- && !(In8(COM1 + 5) & Mask)) {
        __asm__ volatile("pause");
    }
}

static int ProbeCom1(void) {
    UINT8 A;
    UINT8 B;

#if !TOY_SERIAL
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
#if !TOY_SERIAL
    (void)Channel;
    return 0;
#else
    switch (Channel) {
    case TOY_SLOG_BOOT: return TOY_SERIAL_BOOT;
    case TOY_SLOG_USB:  return TOY_SERIAL_USB;
    case TOY_SLOG_SMP:  return TOY_SERIAL_SMP;
    case TOY_SLOG_GUI:  return TOY_SERIAL_GUI;
    case TOY_SLOG_NET:  return TOY_SERIAL_NET;
    case TOY_SLOG_FS:   return TOY_SERIAL_FS;
    case TOY_SLOG_MEM:  return TOY_SERIAL_MEM;
    case TOY_SLOG_DRV:  return TOY_SERIAL_DRV;
    case TOY_SLOG_MISC:
    default:            return TOY_SERIAL_MISC;
    }
#endif
}

static void UartPut(char C) {
#if TOY_SERIAL
    if (!gSerialOk) {
        return;
    }
    WaitBit(0x20);
    Out8(COM1, (UINT8)C);
#else
    (void)C;
#endif
}

static void UartWriteRaw(const char *Text) {
#if !TOY_SERIAL
    (void)Text;
#else
    if (Text == 0) {
        return;
    }
    while (*Text) {
        if (*Text == '\r' && Text[1] == '\n') {
            UartPut('\r');
            UartPut('\n');
            WaitBit(0x40);
            Text += 2;
            continue;
        }
        if (*Text == '\n') {
            UartPut('\r');
            UartPut('\n');
            WaitBit(0x40);
            Text++;
            continue;
        }
        UartPut(*Text++);
    }
#endif
}

void HalSerialInitialize(void) {
#if !TOY_SERIAL
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
        UartWriteRaw("ToyOS X64\n");
        UartWriteRaw("COM1 Serial OK\n");
    } else {
        /* 无 COM1：静默（Boot 阶段可能已报过） */
    }
    gSerialReady = 1;
#endif
}

void HalSerialRetryIfMissing(void) {
#if TOY_SERIAL
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
    HalSerialWriteChannel(TOY_SLOG_MISC, Text);
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
#if !TOY_SERIAL
    return 0;
#else
    return gSerialOk && (In8(COM1 + 5) & 0x01) ? 1 : 0;
#endif
}

char HalSerialReadChar(void) {
#if !TOY_SERIAL
    return 0;
#else
    while (!HalSerialDataReady()) {
        __asm__ volatile("pause");
    }
    return (char)In8(COM1);
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
    HalSerialBootMarkChannel(TOY_SLOG_BOOT, Text);
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
