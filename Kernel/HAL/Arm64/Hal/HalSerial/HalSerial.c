/*
 * HalSerial.c — aarch64 串口 + GOP 镜像门面（PR-NORM-3：对齐 X64 HalSerial/）
 *
 * - virt：PL011
 * - Pi3BPlus 等 BOARD_UART_MINI=1：BCM AUX mini-UART
 * - Pixel 8 等 BOARD_UART_SAMSUNG=1：Exynos/S3C UART（SamsungUart.c）
 * - 屏上滚：HalSerialGop.c（与 X64 同形，勿 stub）
 * - 通道写出：HalSerialWrite.c
 *
 * SERIAL_ENABLE=0：不碰 UART；SCREEN_LOG=0：Gop 横幅/通道全关。
 */
#include "HalSerial.h"
#include "HalSerialPrivate.h"
#include "Text.h"
#include "BoardConfig.h"
#include "SerialConfig.h"
#include "Font.h"

#ifndef BOARD_UART_MINI
#define BOARD_UART_MINI 0
#endif
#ifndef BOARD_UART_SAMSUNG
#define BOARD_UART_SAMSUNG 0
#endif

#if BOARD_UART_SAMSUNG
#include "SamsungUart.h"
#elif BOARD_UART_MINI
#define UART_BASE  ((UINTN)BOARD_UART_BASE)
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
#define UART_BASE  ((UINTN)BOARD_UART_BASE)
#define PL011_DR    (*(volatile UINT32 *)(UART_BASE + 0x00))
#define PL011_FR    (*(volatile UINT32 *)(UART_BASE + 0x18))
#define PL011_FR_TXFF  (1u << 5)
#define PL011_FR_RXFE  (1u << 4)
#endif

static char gRing[GOP_RING];
static UINTN gRingLen;

int gVideoUp;
int gGopBanner;
int gGopMute;
int gGopMirror = 1;
UINT32 gBootLogY;
char gHalSerialLine[BOOT_LOG_LINE_MAX];
UINTN gLineLen;
char gBootVis[BOOT_VIS_MAX][BOOT_LOG_LINE_MAX];
UINT32 gBootVisN;

static void UartPut(char C) {
#if SERIAL_ENABLE
#if BOARD_UART_SAMSUNG
    SamsungUartPutByte((UINT8)C);
#elif BOARD_UART_MINI
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
static void UartWaitTransmitIdle(void) {
    UINT32 Guard = 0;
    while ((MU_LSR & MU_LSR_TX_IDLE) == 0 && Guard < 200000u) {
        __asm__ volatile("nop");
        Guard++;
    }
}
#endif

void UartWriteRaw(const char *Text) {
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
            UartWaitTransmitIdle();
#endif
            continue;
        }
        if (*Text == '\n') {
            UartPut('\r');
            UartPut('\n');
            Text++;
#if BOARD_UART_MINI
            UartWaitTransmitIdle();
#endif
            continue;
        }
        UartPut(*Text++);
    }
#endif
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

#if BOARD_UART_MINI
#define MU_LCR (*(volatile UINT32 *)(UART_BASE + 0x4Cu))
#define MU_LCR_8BIT 3u
#endif

void HalSerialInitialize(void) {
#if SERIAL_ENABLE && BOARD_UART_SAMSUNG
    SamsungUartInitialize();
#elif SERIAL_ENABLE && BOARD_UART_MINI
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

void HalSerialRetryIfMissing(void) {
}

void HalSerialEnableReceiveIrq(void) {
}

int HalSerialPresent(void) {
#if SERIAL_ENABLE
    return 1;
#else
    return 0;
#endif
}

void HalSerialReceivePump(void) {
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

int HalSerialDataReady(void) {
#if !SERIAL_ENABLE
    return 0;
#elif BOARD_UART_SAMSUNG
    return SamsungUartDataReady();
#elif BOARD_UART_MINI
    return (MU_LSR & MU_LSR_DATA_READY) ? 1 : 0;
#else
    return (PL011_FR & PL011_FR_RXFE) ? 0 : 1;
#endif
}

char HalSerialReadChar(void) {
#if !SERIAL_ENABLE
    return 0;
#elif BOARD_UART_SAMSUNG
    return SamsungUartReadByte();
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

void HalSerialPutByte(UINT8 Value) {
    UartPut((char)Value);
}

int HalSerialTryGetByte(UINT8 *Out) {
    if (Out == 0 || HalSerialDataReady() == 0) {
        return 0;
    }
    *Out = (UINT8)HalSerialReadChar();
    return 1;
}

void HalSerialDrainTransmitter(void) {
#if SERIAL_ENABLE && BOARD_UART_SAMSUNG
    SamsungUartDrainTransmitter();
#elif SERIAL_ENABLE && BOARD_UART_MINI
    UartWaitTransmitIdle();
#elif SERIAL_ENABLE
    {
        UINT32 Guard = 0;
        /* PL011：等 TXFF 清且忙位空（FR busy bit3） */
        while ((PL011_FR & (PL011_FR_TXFF | (1u << 3))) != 0 && Guard < 200000u) {
            __asm__ volatile("nop");
            Guard++;
        }
    }
#else
    (void)0;
#endif
}

void HalSerialBootLogRewind(void) {
}

void HalSerialFormatHex(char *Buf, UINT64 Value, int Digits) {
    if (Buf == 0 || Digits <= 0 || Digits > 16) return;
    FormatHexadecimal(Buf, Digits + 1, Value, Digits, 0, 0);
}
