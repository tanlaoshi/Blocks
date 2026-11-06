/*
 * Serial.c — COM1 串口驱动（经 HalIo；PR-H3：探测存在性）
 *
 * SERIAL_ENABLE=0：不 Probe、不碰端口，一切 TX/RX 空操作。
 *
 * RX：IRQ4 → 软环（SPSC）。CoolTerm 整行突发时仅靠 Shell 轮询会冲掉
 * 16550 FIFO(~16)；ISR 在字节到达时抽空。禁止在定时器里自旋抢锁。
 */
#include "Serial/Serial.h"
#include "Text.h"
#include "Hal.h"
#include "HalIrq.h"
#include "IoApic.h"
#include "SerialConfig.h"
#include "Atomic.h"

#define COM1 0x3F8

static int gSerialOk;
static int gSerialInited;
static int gSerialIrqOn;
/* -1=无人；否则 = 独占 UART 的逻辑 CPU（ExceptionHalt） */
static volatile INT32 gSerialExcOwner = -1;

#define SERIAL_RX_RING 512u
static char gReceiveRing[SERIAL_RX_RING];
static volatile UINT32 gReceiveHead; /* ISR / poll producer */
static volatile UINT32 gReceiveTail; /* Shell consumer */
static volatile INT32 gReceiveLock;  /* 仅 poll 路径；IRQ 开后不用 */

static int SerialReceiveTryLock(void) {
    return BLOCKS_ATOMIC_TAS(&gReceiveLock, 1) == 0;
}

static void SerialReceiveUnlock(void) {
    BLOCKS_ATOMIC_RELEASE(&gReceiveLock);
}

static void SerialReceivePush(char C) {
    UINT32 Head = gReceiveHead;
    UINT32 Next = (Head + 1u) % SERIAL_RX_RING;

    if (Next == gReceiveTail) {
        /* 环满：丢最旧，保最新（粘贴尾部常含 CR） */
        gReceiveTail = (gReceiveTail + 1u) % SERIAL_RX_RING;
    }
    gReceiveRing[Head] = C;
    BLOCKS_ATOMIC_FENCE();
    gReceiveHead = Next;
}

static void SerialReceiveDrainHardware(void) {
    int N = 0;

    while ((HalIoRead8(COM1 + 5) & 0x01) != 0 && N < 64) {
        SerialReceivePush((char)HalIoRead8(COM1));
        N++;
    }
}

/* COM1 RX ISR：只抽 HW→环，不抢自旋锁、不调度 */
void SerialIrq(void) {
    if (!gSerialOk) {
        return;
    }
    SerialReceiveDrainHardware();
}

void SerialReceivePump(void) {
    if (!gSerialOk || gSerialIrqOn) {
        /* IRQ 模式下 HW 只由 SerialIrq 读，避免与 ISR 抢 RBR */
        return;
    }
    if (!SerialReceiveTryLock()) {
        return;
    }
    SerialReceiveDrainHardware();
    SerialReceiveUnlock();
}

static int ProbeCom1(void) {
    UINT8 A;
    UINT8 B;

#if !SERIAL_ENABLE
    return 0;
#endif
    /* Scratch 寄存器（offset 7）：无 16550 时常读回 0xFF */
    HalIoWrite8(COM1 + 7, 0x55);
    A = HalIoRead8(COM1 + 7);
    HalIoWrite8(COM1 + 7, 0xAA);
    B = HalIoRead8(COM1 + 7);
    return (A == 0x55 && B == 0xAA) ? 1 : 0;
}

void SerialInitialize(void) {
#if !SERIAL_ENABLE
    gSerialOk = 0;
    gSerialInited = 1;
    return;
#else
    if (gSerialInited) {
        return;
    }
    gSerialOk = ProbeCom1();
    if (!gSerialOk) {
        gSerialInited = 1;
        return;
    }
    HalIoWrite8(COM1 + 1, 0x00); /* IER：先关中断 */
    HalIoWrite8(COM1 + 3, 0x80);
    HalIoWrite8(COM1 + 0, 0x01); /* 115200 */
    HalIoWrite8(COM1 + 1, 0x00);
    HalIoWrite8(COM1 + 3, 0x03); /* 8N1 */
    /*
     * FCR：开 FIFO、清空；触发阈值=1（勿用 14——距溢出只剩 2 字节）。
     * 0x07 = enable|RCVR reset|XMIT reset|trigger1
     */
    HalIoWrite8(COM1 + 2, 0x07);
    HalIoWrite8(COM1 + 4, 0x0B); /* DTR|RTS|OUT2（OUT2 真机才放行 INTR） */
    gSerialInited = 1;
#endif
}

/*
 * ArchInitialize / IoApic 之后调用：ISA IRQ4 → VEC_COM1 → BSP，开 ERBFI。
 * 失败则保持 poll（SerialReceivePump）。
 */
void SerialEnableReceiveIrq(void) {
#if !SERIAL_ENABLE
    return;
#else
    if (!gSerialOk || gSerialIrqOn) {
        return;
    }
    if (!IoApicReady()) {
        return;
    }
    /* 送到 BSP：Shell AP 常 IF=0，IRQ 必须能在开中断的核上抽 FIFO */
    if (IoApicRouteIsaIrq(4, (UINT8)VEC_COM1, HalCpuApicId(0)) != 0) {
        return;
    }
    SerialReceiveDrainHardware(); /* 清残留，避免一开 IER 就风暴 */
    HalIrqNotifyRegister(VEC_COM1, SerialIrq);
    HalIoWrite8(COM1 + 1, 0x01); /* ERBFI：收到数据可中断 */
    gSerialIrqOn = 1;
#endif
}

void SerialRetryIfMissing(void) {
#if !SERIAL_ENABLE
    return;
#else
    if (gSerialOk) {
        return;
    }
    gSerialInited = 0;
    SerialInitialize();
#endif
}

int SerialPresent(void) {
#if !SERIAL_ENABLE
    return 0;
#else
    return gSerialOk;
#endif
}

static void SerialPutChar(char C) {
    int Timeout;

    if (!gSerialOk) {
        return;
    }
    Timeout = 1000000;
    while (Timeout-- && !(HalIoRead8(COM1 + 5) & 0x20)) {
        __asm__ volatile ("pause");
    }
    HalIoWrite8(COM1, (UINT8)C);
}

int SerialDataReady(void) {
    if (!gSerialOk) {
        return 0;
    }
    if (!gSerialIrqOn) {
        SerialReceivePump();
    }
    return gReceiveHead != gReceiveTail;
}

char SerialReadChar(void) {
    char C;
    UINT32 Tail;

    if (!gSerialOk) {
        return 0;
    }
    if (!gSerialIrqOn) {
        UINT32 Spins = 0;

        while (!SerialReceiveTryLock()) {
            __asm__ volatile ("pause");
            if (++Spins > 100000u) {
                return 0;
            }
        }
        SerialReceiveDrainHardware();
        if (gReceiveHead == gReceiveTail) {
            SerialReceiveUnlock();
            return 0;
        }
        Tail = gReceiveTail;
        C = gReceiveRing[Tail];
        gReceiveTail = (Tail + 1u) % SERIAL_RX_RING;
        SerialReceiveUnlock();
        return C;
    }
    /* IRQ 模式：只消费软环，不碰 HW */
    if (gReceiveHead == gReceiveTail) {
        return 0;
    }
    Tail = gReceiveTail;
    C = gReceiveRing[Tail];
    BLOCKS_ATOMIC_FENCE();
    gReceiveTail = (Tail + 1u) % SERIAL_RX_RING;
    return C;
}

void SerialClaimException(void) {
    INT32 Cpu = (INT32)HalCpuGetId();
    INT32 Expected = -1;

    if (BLOCKS_ATOMIC_CAS(&gSerialExcOwner, Expected, Cpu)) {
        return;
    }
    if (gSerialExcOwner == Cpu) {
        return;
    }
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

void SerialWrite(const char *Text) {
    INT32 Own;

    if (!gSerialOk || !Text) {
        return;
    }
    Own = gSerialExcOwner;
    if (Own >= 0 && Own != (INT32)HalCpuGetId()) {
        return;
    }
    while (*Text) {
        if (*Text == '\n') {
            SerialPutChar('\r');
        }
        SerialPutChar(*Text++);
    }
}

void SerialHexFormat(char *Buf, UINT64 Value, int Digits) {
    FormatHexadecimal(Buf, 2 + Digits + 1, Value, Digits, 1, 1);
}

void SerialHex32(UINT32 Value) {
    char Buf[12];
    SerialHexFormat(Buf, Value, 8);
    SerialWrite(Buf);
}

void SerialHex64(UINT64 Value) {
    char Buf[20];
    SerialHexFormat(Buf, Value, 16);
    SerialWrite(Buf);
}
