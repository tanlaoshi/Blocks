/*
 * KsendSerial.c — 协议用的原始字节收发（含二进制 CRC 表）
 *
 * 为什么不用 HalSerial* 写：块 CRC 表是 n*4 字节二进制，里面有 0x00 也有 0x0A，
 * HalSerialWriteShell() 收 const char*、还会把 '\n' 翻成 "\r\n"，写不了。
 * 所以 TX 直写 THR（寄存器布局照抄 HAL/RiscV/Hal/HalSerial.c：reg-shift 见
 * BoardConfig.h，0=THR/RBR，5=LSR）；RX 也在这里轮询，超时可控。
 *
 * 谁在抢 RX：串口壳是同步的——命令处理函数返回前 Shell/Serial.c 的读循环不跑，
 * 且本板 HalSerialEnableRxIrq() 是空实现，所以命令执行期间 UART 只有我们在读。
 */
#include "Ksend/Private.h"
#include "BoardConfig.h"
#include "Hal.h"

#define KSEND_UART_BASE           ((UINTN)BOARD_UART_BASE)
#define KSEND_UART_OFFSET(Reg)    ((UINTN)(Reg) << (BOARD_UART_REG_SHIFT))
#define KSEND_UART_REGISTER_DATA  0u
#define KSEND_UART_REGISTER_LSR   5u
/* LSR 位定义同 HalSerial.c：LSR_DR / LSR_THRE；TEMT 见 16550 手册 bit6 */
#define KSEND_UART_LSR_DATA_READY (1u << 0)
#define KSEND_UART_LSR_THR_EMPTY  (1u << 5)
#define KSEND_UART_LSR_TX_EMPTY   (1u << 6)

#define KSEND_UART_DATA \
    (*(volatile UINT8 *)(KSEND_UART_BASE + KSEND_UART_OFFSET(KSEND_UART_REGISTER_DATA)))
#define KSEND_UART_LSR \
    (*(volatile UINT8 *)(KSEND_UART_BASE + KSEND_UART_OFFSET(KSEND_UART_REGISTER_LSR)))

/* rdtime 是 25 MHz（BoardConfig.h BOARD_TIMEBASE_HZ，cv18xx.dtsi） */
#define KSEND_TIMEBASE_PER_MS     (BOARD_TIMEBASE_HZ / 1000u)
#define KSEND_MILLISECONDS(Value) ((UINT32)((Value) / KSEND_TIMEBASE_PER_MS))

static UINT32 KsendNowMs(void) {
    UINT64 Ticks;

    __asm__ volatile("rdtime %0" : "=r"(Ticks));
    return KSEND_MILLISECONDS(Ticks);
}

static int KsendElapsed(UINT32 Start, UINT32 LimitMs) {
    return ((UINT32)(KsendNowMs() - Start)) >= LimitMs;
}

void KsendSerialPutByte(UINT8 Value) {
    UINT32 Start = KsendNowMs();

    while ((KSEND_UART_LSR & KSEND_UART_LSR_THR_EMPTY) == 0) {
        /* UART 没起时宁可丢字节（PC 会超时重来），也不能把壳挂死在这里 */
        if (KsendElapsed(Start, KSEND_TX_WAIT_MS)) {
            return;
        }
        HalCpuRelax();
    }
    KSEND_UART_DATA = Value;
}

void KsendSerialWrite(const char *Text) {
    if (Text == 0) {
        return;
    }
    while (*Text != 0) {
        if (*Text == '\n') {
            KsendSerialPutByte((UINT8)'\r');
            KsendSerialPutByte((UINT8)'\n');
        } else {
            KsendSerialPutByte((UINT8)(*Text));
        }
        Text++;
    }
}

int KsendSerialGetByte(UINT8 *Out, UINT32 TimeoutMs) {
    UINT32 Start = KsendNowMs();

    for (;;) {
        if ((KSEND_UART_LSR & KSEND_UART_LSR_DATA_READY) != 0) {
            *Out = KSEND_UART_DATA;
            return 1;
        }
        if (KsendElapsed(Start, TimeoutMs)) {
            return 0;
        }
        HalCpuRelax();
    }
}

void KsendSerialDrainInput(UINT32 QuietMs, UINT32 LimitMs) {
    UINT32 Start = KsendNowMs();
    UINT32 Last = Start;

    for (;;) {
        if ((KSEND_UART_LSR & KSEND_UART_LSR_DATA_READY) != 0) {
            (void)KSEND_UART_DATA; /* 读 THR 即清 DR；这里的字节没人认领 */
            Last = KsendNowMs();
        } else if (KsendElapsed(Last, QuietMs) || KsendElapsed(Start, LimitMs)) {
            return;
        }
    }
}

void KsendSerialDrainTransmitter(void) {
    UINT32 Start = KsendNowMs();

    while ((KSEND_UART_LSR & KSEND_UART_LSR_TX_EMPTY) == 0) {
        if (KsendElapsed(Start, KSEND_TX_WAIT_MS)) {
            return;
        }
        HalCpuRelax();
    }
}

UINT32 KsendMilliseconds(void) {
    return KsendNowMs();
}

void KsendDelayMs(UINT32 Milliseconds) {
    UINT32 Start = KsendNowMs();

    while (!KsendElapsed(Start, Milliseconds)) {
        HalCpuRelax();
    }
}
