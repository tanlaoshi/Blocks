/*
 * I2c.c — Raspberry Pi 3B 的硬件 I2C1（BCM2837 BSC1）
 *
 * 从哪读：BCM2835 ARM Peripherals §3「BSC」
 *   0x00 C      控制：bit15 I2CEN 使能、bit8 INT_RD 读模式、bit7 ST 起始、
 *                     bit4 CLEAR 清 FIFO、bit0 READ
 *   0x04 S      状态：bit9 DONE 传输完、bit8 TXFE 发送 FIFO 空、bit7 RXDF 收到数据、
 *                     bit1 ERR、bit0 TA 忙
 *   0x08 DLEN   数据长度（1..16）
 *   0x0C A      slave 地址（写地址<<1）
 *   0x10 FIFO   16 字节数据 FIFO
 *   0x14 DIV    时钟分频：SCL = core_clk / (DIV * 2)
 *
 * 接线（本板实际用）：GPIO2 = SDA1、GPIO3 = SCL1 → GPFSEL0 里两脚都设 ALT0(4)
 *   上拉由树莓派板载 1.8k 电阻提供，内部上下拉关掉即可。
 *
 * 别改：
 *   · 每次传输前清 FIFO 与状态，否则上一笔的残留会让下一笔误判成功；
 *   · 超时必须返回错误，不能死等（I2C 挂死会拖垮整个启动）。
 */
#include "HalI2c.h"
#include "Hal.h"

#define RPI_BSC1_BASE          0x3F804000ULL
#define RPI_BSC_C              0x00u
#define RPI_BSC_S              0x04u
#define RPI_BSC_DLEN           0x08u
#define RPI_BSC_A              0x0Cu
#define RPI_BSC_FIFO           0x10u
#define RPI_BSC_DIV            0x14u

#define RPI_BSC_C_I2CEN        (1u << 15)
#define RPI_BSC_C_INT_RD       (1u << 8)
#define RPI_BSC_C_ST           (1u << 7)
#define RPI_BSC_C_CLEAR        (3u << 4)
#define RPI_BSC_C_READ         (1u << 0)

#define RPI_BSC_S_DONE         (1u << 9)
#define RPI_BSC_S_TXFE         (1u << 8)
#define RPI_BSC_S_RXDF         (1u << 7)
#define RPI_BSC_S_ERR          (1u << 1)
#define RPI_BSC_S_TA           (1u << 0)

#define RPI_GPIO_BASE          0x3F200000ULL
#define RPI_GPIO_GPFSEL0       0x00u
#define RPI_GPIO_GPLEV0        0x34u
#define RPI_GPIO_PIN2_SHIFT    6u
#define RPI_GPIO_PIN3_SHIFT    9u
#define RPI_GPIO_FSEL_MASK     7u
#define RPI_GPIO_FSEL_ALT0     4u

#define RPI_BSC_CORE_CLOCK_HZ  250000000u
#define RPI_BSC_TARGET_HZ      100000u
#define RPI_BSC_DIV_VALUE      (RPI_BSC_CORE_CLOCK_HZ / (RPI_BSC_TARGET_HZ * 2u))
#define RPI_BSC_TIMEOUT_LOOPS  100000u
#define RPI_I2C_ADDRESS_MAX    0x7Fu

static volatile UINT32 *RpiBsc(UINT32 Offset) {
    return (volatile UINT32 *)(UINTN)(RPI_BSC1_BASE + (UINT64)Offset);
}

static volatile UINT32 *RpiGpio(UINT32 Offset) {
    return (volatile UINT32 *)(UINTN)(RPI_GPIO_BASE + (UINT64)Offset);
}

static void RpiI2cReset(void);

static void RpiI2cSetPinFunction(UINT32 Shift) {
    volatile UINT32 *Select = RpiGpio(RPI_GPIO_GPFSEL0);
    UINT32 Value = *Select;

    Value &= ~(RPI_GPIO_FSEL_MASK << Shift);
    Value |= (RPI_GPIO_FSEL_ALT0 & RPI_GPIO_FSEL_MASK) << Shift;
    *Select = Value;
}

static UINT32 sRpiI2cReady;
static int sRpiI2cLastProbe; /* 最后一次探测的返回码（诊断用） */

/*
 * 懒初始化：把 GPIO2/3 切到 ALT0。为什么不在 Board*Init 里做 ——
 * Pi 的板级没有统一的 init 挂点，写成懒加载最不容易漏，也便于单测。
 */
static void RpiI2cEnsureReady(void) {
    if (sRpiI2cReady != 0u) {
        return;
    }
    RpiI2cSetPinFunction(RPI_GPIO_PIN2_SHIFT);
    RpiI2cSetPinFunction(RPI_GPIO_PIN3_SHIFT);
    RpiI2cReset();
    sRpiI2cReady = 1u;
}

int HalI2cAvailable(void) {
    RpiI2cEnsureReady();
    return 1;
}

static void RpiI2cReset(void) {
    *RpiBsc(RPI_BSC_C) = 0u;
    *RpiBsc(RPI_BSC_S) = RPI_BSC_S_DONE | RPI_BSC_S_ERR;
    *RpiBsc(RPI_BSC_C) = RPI_BSC_C_CLEAR;
}

static void RpiI2cConfigure(UINT8 Address, UINT32 Length, int Read) {
    RpiI2cReset();
    *RpiBsc(RPI_BSC_DIV) = RPI_BSC_DIV_VALUE;
    *RpiBsc(RPI_BSC_A) = (UINT32)Address;
    *RpiBsc(RPI_BSC_DLEN) = Length;
    *RpiBsc(RPI_BSC_C) = RPI_BSC_C_I2CEN | (Read != 0 ? (RPI_BSC_C_READ | RPI_BSC_C_INT_RD) : 0u);
}

/* 等传输结束：0 = 成功；-1 = 超时；-2 = 总线错误 */
static int RpiI2cWaitDone(void) {
    UINT32 Guard = RPI_BSC_TIMEOUT_LOOPS;
    UINT32 Status;

    while (Guard-- > 0u) {
        Status = *RpiBsc(RPI_BSC_S);
        if ((Status & RPI_BSC_S_DONE) != 0u) {
            return ((Status & RPI_BSC_S_ERR) != 0u) ? -2 : 0;
        }
        if ((Status & RPI_BSC_S_ERR) != 0u) {
            return -2;
        }
    }
    return -1;
}

static int RpiI2cStartTransfer(UINT32 Length) {
    (void)Length; /* 长度已写进 DLEN；这里只负责发起并等完成 */
    *RpiBsc(RPI_BSC_C) |= RPI_BSC_C_ST;
    return RpiI2cWaitDone();
}

static int RpiI2cWriteBytes(UINT8 Address, const UINT8 *Data, UINT32 Length) {
    UINT32 Index;
    int Status;

    if (Length == 0u || Length > 16u) {
        return -1;
    }
    RpiI2cConfigure(Address, Length, 0);
    for (Index = 0u; Index < Length; Index++) {
        *RpiBsc(RPI_BSC_FIFO) = Data[Index];
    }
    Status = RpiI2cStartTransfer(Length);
    RpiI2cReset();
    return Status;
}

static int RpiI2cReadBytes(UINT8 Address, UINT8 *Buffer, UINT32 Length) {
    UINT32 Index;
    UINT32 Guard;
    int Status;

    if (Length == 0u || Length > 16u) {
        return -1;
    }
    RpiI2cConfigure(Address, Length, 1);
    Status = RpiI2cStartTransfer(Length);
    if (Status != 0) {
        RpiI2cReset();
        return Status;
    }
    for (Index = 0u; Index < Length; Index++) {
        Guard = RPI_BSC_TIMEOUT_LOOPS;
        while (((*RpiBsc(RPI_BSC_S)) & RPI_BSC_S_RXDF) == 0u) {
            if (Guard-- == 0u) {
                RpiI2cReset();
                return -1;
            }
        }
        Buffer[Index] = (UINT8)(*RpiBsc(RPI_BSC_FIFO) & 0xFFu);
    }
    RpiI2cReset();
    return 0;
}

int HalI2cProbe(UINT8 Address) {
    UINT8 Dummy = 0u;

    if (Address > RPI_I2C_ADDRESS_MAX) {
        return -1;
    }
    RpiI2cEnsureReady();
    /* 探测 = 写 1 字节，有 ACK 就说明器件在 */
    sRpiI2cLastProbe = RpiI2cWriteBytes((UINT8)(Address << 1), &Dummy, 1u);
    return (sRpiI2cLastProbe == 0) ? 0 : -1;
}

int HalI2cRead(UINT8 Address, UINT8 Register, UINT8 *Buffer, UINT32 Length) {
    if (Buffer == 0 || Address > RPI_I2C_ADDRESS_MAX) {
        return -1;
    }
    RpiI2cEnsureReady();
    if (RpiI2cWriteBytes((UINT8)(Address << 1), &Register, 1u) != 0) {
        return -1;
    }
    return RpiI2cReadBytes((UINT8)((Address << 1) | 1u), Buffer, Length);
}

int HalI2cWrite(UINT8 Address, UINT8 Register, const UINT8 *Buffer, UINT32 Length) {
    UINT8 Frame[17];
    UINT32 Index;

    if (Buffer == 0 || Length == 0u || Length > 16u || Address > RPI_I2C_ADDRESS_MAX) {
        return -1;
    }
    RpiI2cEnsureReady();
    Frame[0] = Register;
    for (Index = 0u; Index < Length; Index++) {
        Frame[Index + 1u] = Buffer[Index];
    }
    return RpiI2cWriteBytes((UINT8)(Address << 1), Frame, Length + 1u);
}

int HalI2cLineLevels(int *OutSdaHigh, int *OutSclHigh) {
    volatile UINT32 *Select;
    UINT32 Value;
    UINT32 Level;
    UINT32 Spin;

    if (OutSdaHigh == 0 || OutSclHigh == 0) {
        return -1;
    }
    Select = RpiGpio(RPI_GPIO_GPFSEL0);
    Value = *Select;
    Value &= ~((RPI_GPIO_FSEL_MASK << RPI_GPIO_PIN2_SHIFT) |
               (RPI_GPIO_FSEL_MASK << RPI_GPIO_PIN3_SHIFT));
    *Select = Value;                    /* 切输入 */
    for (Spin = 0u; Spin < 1000u; Spin++) {
        __asm__ volatile("nop");
    }
    Level = *RpiGpio(RPI_GPIO_GPLEV0);
    *OutSdaHigh = (int)((Level >> 2) & 1u);
    *OutSclHigh = (int)((Level >> 3) & 1u);
    RpiI2cSetPinFunction(RPI_GPIO_PIN2_SHIFT);   /* 恢复 ALT0 */
    RpiI2cSetPinFunction(RPI_GPIO_PIN3_SHIFT);
    return 0;
}

int HalI2cDiagRead(UINT32 Out[5]) {
    if (Out == 0) {
        return -1;
    }
    RpiI2cEnsureReady();
    Out[0] = *RpiGpio(RPI_GPIO_GPFSEL0);
    Out[1] = *RpiBsc(RPI_BSC_DIV);
    Out[2] = *RpiBsc(RPI_BSC_C);
    Out[3] = *RpiBsc(RPI_BSC_S);
    Out[4] = (UINT32)sRpiI2cLastProbe;
    return 0;
}

/* 由板级初始化调用一次：把 GPIO2/3 切到 ALT0（I2C1） */
void BoardI2cInitialize(void) {
    RpiI2cSetPinFunction(RPI_GPIO_PIN2_SHIFT);
    RpiI2cSetPinFunction(RPI_GPIO_PIN3_SHIFT);
    RpiI2cReset();
}
