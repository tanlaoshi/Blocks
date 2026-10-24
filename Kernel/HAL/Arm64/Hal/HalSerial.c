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
#define MU_LSR_TX_EMPTY   (1u << 5)
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
#else
    while (PL011_FR & PL011_FR_TXFF) {
    }
    PL011_DR = (UINT32)(UINT8)C;
#endif
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

void HalSerialInitialize(void) {
#if SERIAL_ENABLE && BOARD_UART_MINI
    /* 跟 U-Boot：只开使能，不改波特（固件已配）；须 TX+RX 都开 */
    MU_ENABLES |= 1u;
    MU_CNTL |= (MU_CNTL_RXEN | MU_CNTL_TXEN);
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
    /*
     * ShellOwn 只表示「COM1 交互壳已接管」——仍须把 Log* 打到 UART。
     * 旧逻辑直接 return → 桌面 Announce 后 CoolTerm 全黑，但 RX 仍能收 halt。
     * GOP 镜像仍受 Mute/Mirror（桌面后 Mirror=0）。
     */
    if (ChannelUartOn(Channel)) {
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
    RingAppend(Text);
    if (!gShellOwnUart && ChannelUartOn(SLOG_BOOT)) {
        UartWriteRaw(Text);
    }
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
    /* BootMark 始终上 UART（ShellOwn 不挡）；屏仍认 Mirror */
    if (ChannelUartOn(Channel)) {
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
