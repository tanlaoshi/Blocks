/*
 * HalSerial.c — aarch64 串口 + GOP 镜像门面
 *
 * - virt：PL011
 * - Pi3BPlus 等 BOARD_UART_MINI=1：BCM AUX mini-UART
 * - 屏上滚：HalSerial/HalSerialGop.c（与 X64 同形，勿 stub）
 *
 * SERIAL_ENABLE=0：不碰 UART；SCREEN_LOG=0：Gop 横幅/通道全关。
 */
#include "HalSerial.h"
#include "HalSerial/HalSerialPrivate.h"
#include "Text.h"
#include "BoardConfig.h"
#include "SerialConfig.h"
#include "Font.h"

#ifndef BOARD_UART_MINI
#define BOARD_UART_MINI 0
#endif

#define UART_BASE  ((UINTN)BOARD_UART_BASE)

#if BOARD_UART_MINI
/* AUX @0x3F215000；mini-UART 寄存器窗 @+0x40（BCM2835 §2.2） */
#define MU_IO      (*(volatile UINT32 *)(UART_BASE + 0x40))
#define MU_LSR     (*(volatile UINT32 *)(UART_BASE + 0x54))
#define MU_CNTL    (*(volatile UINT32 *)(UART_BASE + 0x60))
#define MU_ENABLES (*(volatile UINT32 *)(UART_BASE + 0x04))
#define MU_LSR_TX_EMPTY   (1u << 5) /* FIFO 可再收至少 1 字节 */
#define MU_LSR_TX_IDLE    (1u << 6) /* FIFO 空且移位器空闲（BCM2835 §2.2） */
#define MU_LSR_DATA_READY (1u << 0)
#define MU_CNTL_RXEN       (1u << 0)
#define MU_CNTL_TXEN       (1u << 1)
#else
#define PL011_DR    (*(volatile UINT32 *)(UART_BASE + 0x00))
#define PL011_FR    (*(volatile UINT32 *)(UART_BASE + 0x18))
#define PL011_FR_TXFF  (1u << 5)
#define PL011_FR_RXFE  (1u << 4)
#endif

static int gShellOwnUart;
static char gRing[GOP_RING];
static UINTN gRingLen;
static char gBootSnap[GOP_RING];
static UINTN gBootSnapLen;
/* 对齐 X64：ShellOwn 后 Log* → 缓冲 → RUNTIME*.LOG，不再占 UART */
#define RT_LOG_MAX 16384u
static char gRtLog[RT_LOG_MAX];
static UINTN gRtLen;
static int gRtDirty;

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

int gVideoUp;
int gGopBanner;
int gGopMute;
int gGopMirror = 1;
UINT32 gBootLogY;
char gHalSerialLine[BOOT_LOG_LINE_MAX];
UINTN gLineLen;
char gBootVis[BOOT_VIS_MAX][BOOT_LOG_LINE_MAX];
UINT32 gBootVisN;

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

static void UartPut(char C) {
#if SERIAL_ENABLE
#if BOARD_UART_MINI
    while ((MU_LSR & MU_LSR_TX_EMPTY) == 0) {
    }
    MU_IO = (UINT32)(UINT8)C;
    __asm__ volatile("dsb sy" ::: "memory");
#else
    while (PL011_FR & PL011_FR_TXFF) {
    }
    PL011_DR = (UINT32)(UINT8)C;
#endif
#else
    (void)C;
#endif
}

#if BOARD_UART_MINI && SERIAL_ENABLE
/* 行末等到移位器空：mini-UART 无流控，暴打时 CoolTerm/FTDI 易丢字 */
static void UartWaitTxIdle(void) {
    UINT32 Guard = 0;
    while ((MU_LSR & MU_LSR_TX_IDLE) == 0 && Guard < 200000u) {
        __asm__ volatile("nop");
        Guard++;
    }
}
#endif

static void UartWriteRaw(const char *Text) {
#if !SERIAL_ENABLE
    (void)Text;
#else
    if (Text == 0) {
        return;
    }
#if BOARD_UART_MINI
    /* FB dcache 误扫外设后 TX 位可能被清；每次输出前拉回 */
    MU_ENABLES |= 1u;
    MU_CNTL |= (MU_CNTL_RXEN | MU_CNTL_TXEN);
#endif
    while (*Text) {
        if (*Text == '\r' && Text[1] == '\n') {
            UartPut('\r');
            UartPut('\n');
            Text += 2;
#if BOARD_UART_MINI
            UartWaitTxIdle();
#endif
            continue;
        }
        if (*Text == '\n') {
            UartPut('\r');
            UartPut('\n');
            Text++;
#if BOARD_UART_MINI
            UartWaitTxIdle();
#endif
            continue;
        }
        UartPut(*Text++);
    }
#endif
}

void HalSerialBootCapture(const char *Text) {
    while (Text && *Text) {
        if (gBootSnapLen + 1 >= sizeof(gBootSnap)) {
            break;
        }
        gBootSnap[gBootSnapLen++] = *Text++;
    }
}

void RingAppend(const char *Text) {
    HalSerialBootCapture(Text);
    while (Text && *Text) {
        if (gRingLen + 1 >= GOP_RING) {
            UINTN Keep = GOP_RING / 2;
            UINTN i;
            for (i = 0; i < Keep; i++) {
                gRing[i] = gRing[gRingLen - Keep + i];
            }
            gRingLen = Keep;
        }
        gRing[gRingLen++] = *Text++;
    }
}

static void GopMirrorLine(const char *Text) {
    if (!Text || !gGopMirror || !gVideoUp || gGopMute) {
        return;
    }
    GopWrite(Text);
}

#if BOARD_UART_MINI
#define MU_LCR (*(volatile UINT32 *)(UART_BASE + 0x4Cu))
#define MU_LCR_8BIT 3u
#endif

void HalSerialInitialize(void) {
#if SERIAL_ENABLE && BOARD_UART_MINI
    /*
     * GPIO14/15 → ALT5（mini-UART）。BCM2835 §6 GPFSEL1：
     * FSEL14 bits[14:12]、FSEL15 bits[17:15]；ALT5=0b010。
     * 若被改成别的复用，会出现 RX 偶发仍通、TX 全哑。
     */
    {
        volatile UINT32 *Gpfsel1 =
            (volatile UINT32 *)(UINTN)0x3F200004ULL;
        UINT32 V = *Gpfsel1;

        V &= ~((7u << 12) | (7u << 15));
        V |= (2u << 12) | (2u << 15); /* ALT5 */
        *Gpfsel1 = V;
        __asm__ volatile("dsb sy" ::: "memory");
    }
    MU_ENABLES |= 1u;
    /* 8-bit；不改 MU_BAUD（跟 U-Boot/固件） */
    MU_LCR = MU_LCR_8BIT;
    MU_CNTL |= (MU_CNTL_RXEN | MU_CNTL_TXEN);
    __asm__ volatile("dsb sy" ::: "memory");
    /* 最早心跳：此时尚无 FB dcache；CoolTerm 若仍无字 → 线/口问题 */
    UartWriteRaw("Blocks uart\r\n");
#endif
}

static void BootHoldSpinMs(UINT32 Ms) {
    UINT32 m;
    for (m = 0; m < Ms; m++) {
        volatile UINT32 i;
        for (i = 0; i < 80000u; i++) {
            __asm__ volatile("nop");
        }
    }
}

void HalSerialBootHoldMs(UINT32 Ms) {
    char Hex[9];

    if (Ms == 0) {
        return;
    }
    HalSerialBootMark("Boot: HOLD — CoolTerm expect: U-Boot then * + Blocks uart\n");
    HalSerialBootMark("Boot: HOLD — if U-Boot quiet: check GPIO14->TTL RX\n");
#if SERIAL_ENABLE && BOARD_UART_MINI
    HalSerialInitialize();
    HalSerialBootMark("Boot: uart GPFSEL1=");
    HalSerialFormatHex(Hex, *(volatile UINT32 *)(UINTN)0x3F200004ULL, 8);
    HalSerialBootMark(Hex);
    HalSerialBootMark(" CNTL=");
    HalSerialFormatHex(Hex, MU_CNTL, 8);
    HalSerialBootMark(Hex);
    HalSerialBootMark(" LSR=");
    HalSerialFormatHex(Hex, MU_LSR, 8);
    HalSerialBootMark(Hex);
    HalSerialBootMark(" LCR=");
    HalSerialFormatHex(Hex, MU_LCR, 8);
    HalSerialBootMark(Hex);
    HalSerialBootMark("\n");
    /* 慢打；仍无 UART-HOLD → TX 线/口（halt 若来自 HDMI 壳则未证明 RX） */
    UartWriteRaw("UART-HOLD\r\n");
    {
        UINTN N;
        char Snap[512];
        UINTN i;

        N = HalSerialBootSnapshot(Snap, sizeof(Snap));
        for (i = 0; i < N; i++) {
            char One[2];
            One[0] = Snap[i];
            One[1] = 0;
            UartWriteRaw(One);
            if (Snap[i] == '\n') {
                BootHoldSpinMs(15);
            }
        }
    }
#endif
    BootHoldSpinMs(Ms);
    HalSerialBootMark("Boot: HOLD end\n");
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

void HalSerialBootFontApply(void) {
    (void)FontSetById(0);
}

void HalSerialGopEnable(void) {
    HalSerialBootFontApply();
    if (gVideoUp) {
        return;
    }
    gVideoUp = 1;
    gGopBanner = 0;
    gBootVisN = 0;
    gBootLogY = BootLogBodyY(BootLogLineH());
    gLineLen = 0;
#if SCREEN_LOG
    GopBannerOnce();
#else
    gGopBanner = 1;
#endif
}

void HalSerialGopMirror(int Enable) {
    gGopMirror = Enable ? 1 : 0;
}

int HalSerialGopMirroring(void) {
    return (gGopMirror && gVideoUp && !gGopMute) ? 1 : 0;
}

void HalSerialGopMute(int Mute) {
    gGopMute = Mute ? 1 : 0;
}

const char *HalSerialLogText(void) {
    if (gRingLen >= GOP_RING) {
        gRingLen = GOP_RING - 1;
    }
    gRing[gRingLen] = '\0';
    return gRing;
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

void HalSerialFormatHex(char *Buf, UINT64 Value, int Digits) {
    if (Buf == 0 || Digits <= 0 || Digits > 16) {
        return;
    }
    FormatHexadecimal(Buf, Digits + 1, Value, Digits, 0, 0);
}
