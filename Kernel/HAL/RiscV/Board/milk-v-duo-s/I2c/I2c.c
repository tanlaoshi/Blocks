/*
 * I2c.c — Milk-V Duo S 的 I2C（**软件 bit-bang**，不是硬件 I2C）
 *
 * 为什么不用硬件 I2C：查过厂商 pinmux（cv181x_pinlist_swconfig.h + board_init）后确认
 *   I2C1 的脚 = SD0/SD1（我们的块设备在用）
 *   IIC0  = 已配成 GPIO（LED / GPIOA28）
 *   IIC2/IIC3 = 摄像头；IIC4 = 触摸屏
 * ⇒ Duo S 上没有空闲的硬件 I2C 引脚，只能软件模拟。
 *
 * 引脚：用 GPIO 白名单里已有的 GPIOA18(SDA) / GPIOA19(SCL)（JTAG pad，板级把它复用成 GPIO）。
 *   这两脚在 milk-v-duo-s/Gpio/Gpio.c 的白名单里；上拉由外部器件或 pad 提供，本文件不配上下拉。
 *
 * 时序：标准模式 ~50 kHz（每次电平变化后空转若干次）。不追求速度，只求能读传感器/LCD。
 *
 * 别改：
 *   · 开漏模拟必须"输出低 = 驱动 0，释放 = 切输入"，不能用"输出高"（会和器件的上拉对顶）；
 *   · 每次 Start/Stop/字节后都要给够建立时间，否则器件不认；
 *   · 时钟拉伸（SCL 被从机拉低）要处理：释放 SCL 后等它变高。
 */
#include "HalI2c.h"
#include "HalGpio.h"

#define DUO_I2C_SDA_PIN        18u
#define DUO_I2C_SCL_PIN        19u
#define DUO_I2C_DELAY_LOOPS    40u
#define DUO_I2C_STRETCH_LOOPS  4000u
#define DUO_I2C_ADDRESS_MAX    0x7Fu

static void DuoI2cDelay(void) {
    volatile UINT32 Index;

    for (Index = 0u; Index < DUO_I2C_DELAY_LOOPS; Index++) {
        __asm__ volatile("nop");
    }
}

/* 开漏：Low=驱动 0；High=切输入（释放，靠上拉拉高） */
static void DuoI2cRelease(UINT32 Pin) {
    (void)HalGpioSetMode(Pin, HAL_GPIO_MODE_INPUT);
}

static void DuoI2cDriveLow(UINT32 Pin) {
    (void)HalGpioWrite(Pin, 0);
    (void)HalGpioSetMode(Pin, HAL_GPIO_MODE_OUTPUT);
}

static int DuoI2cReadPin(UINT32 Pin) {
    int High = 0;

    (void)HalGpioRead(Pin, &High);
    return High;
}

static void DuoI2cBusInit(void) {
    DuoI2cRelease(DUO_I2C_SDA_PIN);
    DuoI2cRelease(DUO_I2C_SCL_PIN);
    DuoI2cDelay();
}

/* Start：SCL 高时 SDA 由高变低 */
static void DuoI2cStart(void) {
    DuoI2cRelease(DUO_I2C_SDA_PIN);
    DuoI2cRelease(DUO_I2C_SCL_PIN);
    DuoI2cDelay();
    DuoI2cDriveLow(DUO_I2C_SDA_PIN);
    DuoI2cDelay();
    DuoI2cDriveLow(DUO_I2C_SCL_PIN);
    DuoI2cDelay();
}

/* Stop：SCL 高时 SDA 由低变高 */
static void DuoI2cStop(void) {
    DuoI2cDriveLow(DUO_I2C_SDA_PIN);
    DuoI2cDelay();
    DuoI2cRelease(DUO_I2C_SCL_PIN);
    DuoI2cDelay();
    DuoI2cRelease(DUO_I2C_SDA_PIN);
    DuoI2cDelay();
}

/* 释放 SCL 后等它真的变高（从机可能拉伸时钟）；超时返回 -1 */
static int DuoI2cReleaseClock(void) {
    UINT32 Guard = DUO_I2C_STRETCH_LOOPS;

    DuoI2cRelease(DUO_I2C_SCL_PIN);
    while (DuoI2cReadPin(DUO_I2C_SCL_PIN) == 0) {
        if (Guard-- == 0u) {
            return -1;
        }
    }
    return 0;
}

/* 写 1 位；返回 0 = 成功 */
static int DuoI2cWriteBit(int Bit) {
    if (Bit != 0) {
        DuoI2cRelease(DUO_I2C_SDA_PIN);
    } else {
        DuoI2cDriveLow(DUO_I2C_SDA_PIN);
    }
    DuoI2cDelay();
    if (DuoI2cReleaseClock() != 0) {
        return -1;
    }
    DuoI2cDelay();
    DuoI2cDriveLow(DUO_I2C_SCL_PIN);
    DuoI2cDelay();
    return 0;
}

/* 读 1 位（先释放 SDA，再放时钟） */
static int DuoI2cReadBit(int *OutBit) {
    int Bit;

    if (OutBit == 0) {
        return -1;
    }
    DuoI2cRelease(DUO_I2C_SDA_PIN);
    DuoI2cDelay();
    if (DuoI2cReleaseClock() != 0) {
        return -1;
    }
    DuoI2cDelay();
    Bit = DuoI2cReadPin(DUO_I2C_SDA_PIN);
    DuoI2cDriveLow(DUO_I2C_SCL_PIN);
    DuoI2cDelay();
    *OutBit = Bit;
    return 0;
}

/* 发一个字节 + 读 ACK；返回 0 = 有 ACK */
static int DuoI2cWriteByte(UINT8 Value) {
    UINT32 Bit;
    int Ack = 0;

    for (Bit = 0u; Bit < 8u; Bit++) {
        if (DuoI2cWriteBit((Value & 0x80u) != 0u ? 1 : 0) != 0) {
            return -1;
        }
        Value = (UINT8)(Value << 1);
    }
    if (DuoI2cReadBit(&Ack) != 0) {
        return -1;
    }
    return (Ack == 0) ? 0 : -1;
}

static int DuoI2cReadByte(UINT8 *Out, int SendAck) {
    UINT32 Bit;
    UINT8 Value = 0u;
    int Got = 0;

    if (Out == 0) {
        return -1;
    }
    for (Bit = 0u; Bit < 8u; Bit++) {
        if (DuoI2cReadBit(&Got) != 0) {
            return -1;
        }
        Value = (UINT8)((Value << 1) | (UINT8)(Got != 0 ? 1 : 0));
    }
    if (DuoI2cWriteBit(SendAck != 0 ? 0 : 1) != 0) {
        return -1;
    }
    *Out = Value;
    return 0;
}

int HalI2cAvailable(void) {
    return 1;
}

int HalI2cProbe(UINT8 Address) {
    int Status;

    if (Address > DUO_I2C_ADDRESS_MAX) {
        return -1;
    }
    DuoI2cBusInit();
    DuoI2cStart();
    Status = DuoI2cWriteByte((UINT8)(Address << 1));
    DuoI2cStop();
    return Status;
}

int HalI2cWrite(UINT8 Address, UINT8 Register, const UINT8 *Buffer, UINT32 Length) {
    UINT32 Index;

    if (Buffer == 0 || Length == 0u || Address > DUO_I2C_ADDRESS_MAX) {
        return -1;
    }
    DuoI2cBusInit();
    DuoI2cStart();
    if (DuoI2cWriteByte((UINT8)(Address << 1)) != 0 || DuoI2cWriteByte(Register) != 0) {
        DuoI2cStop();
        return -1;
    }
    for (Index = 0u; Index < Length; Index++) {
        if (DuoI2cWriteByte(Buffer[Index]) != 0) {
            DuoI2cStop();
            return -1;
        }
    }
    DuoI2cStop();
    return 0;
}

int HalI2cRead(UINT8 Address, UINT8 Register, UINT8 *Buffer, UINT32 Length) {
    UINT32 Index;

    if (Buffer == 0 || Length == 0u || Address > DUO_I2C_ADDRESS_MAX) {
        return -1;
    }
    DuoI2cBusInit();
    /* 写寄存器号（无 Stop）→ 重复 Start → 读 */
    DuoI2cStart();
    if (DuoI2cWriteByte((UINT8)(Address << 1)) != 0 || DuoI2cWriteByte(Register) != 0) {
        DuoI2cStop();
        return -1;
    }
    DuoI2cStart();
    if (DuoI2cWriteByte((UINT8)((Address << 1) | 1u)) != 0) {
        DuoI2cStop();
        return -1;
    }
    for (Index = 0u; Index < Length; Index++) {
        if (DuoI2cReadByte(&Buffer[Index], (Index + 1u) < Length) != 0) {
            DuoI2cStop();
            return -1;
        }
    }
    DuoI2cStop();
    return 0;
}

int HalI2cDiagRead(UINT32 Out[5]) {
    if (Out == 0) {
        return -1;
    }
    Out[0] = DUO_I2C_SDA_PIN;
    Out[1] = DUO_I2C_SCL_PIN;
    Out[2] = 0u;
    Out[3] = 0u;
    Out[4] = 0u;
    return 0;
}

int HalI2cLineLevels(int *OutSdaHigh, int *OutSclHigh) {
    if (OutSdaHigh == 0 || OutSclHigh == 0) {
        return -1;
    }
    DuoI2cBusInit();
    *OutSdaHigh = DuoI2cReadPin(DUO_I2C_SDA_PIN);
    *OutSclHigh = DuoI2cReadPin(DUO_I2C_SCL_PIN);
    return 0;
}
