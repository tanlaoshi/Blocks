/*
 * Gpio.c — Raspberry Pi 3B 的 GPIO（BCM2835/2837 §6）
 *
 * 从哪读：BCM2835 ARM Peripherals §6「General Purpose I/O」
 *   0x00 GPFSEL0..5   每 10 个脚一个寄存器，每个脚占 3 bit（000=输入，001=输出）
 *   0x1C GPSET0/1     置高（写 1 生效，写 0 无效）
 *   0x28 GPCLR0/1     置低
 *   0x34 GPLEV0/1     读【引脚实际电平】（输出模式下也能读回，自检靠它）
 *   0x40 GPEDS0/1     边沿事件状态（写 1 清）
 *   0x4C GPREN0/1     同步上升沿检测使能（常规路径；Linux bcm2835-gpio 用它做 IRQ）
 *   0x58 GPFEN0/1     同步下降沿检测使能
 *   0x7C GPAREN0/1    异步上升沿（低功耗唤醒用，**不是**常规检测）
 *   0x88 GPAFEN0/1    异步下降沿（同上）
 *   0x94 GPPUD        上下拉：0=关 1=下拉 2=上拉
 *   0x98 GPPUDCLK0/1  把 GPPUD 打到指定脚；之后必须清 GPPUD 与 GPPUDCLK
 *
 * 时序：GPPUD 生效需要 ~150 个周期，之后清；照 2835 §6.1 的建议在 PUDCLK 前后各等一次。
 *       （同样的 150 次 nop 在 HAL/Arm64/Hal/Hal.c 的电源键代码里已用过，行为一致。）
 *
 * 别改：读回一定走 GPLEV，不要改成"读回最后一次写的值"——那样自检就没意义了。
 *       边沿取走必须写 GPEDS 清位；Enable 前先清陈旧事件。
 */
#include "HalGpio.h"
#include "Hal.h"

#define RPI_GPIO_BASE                 0x3F200000ULL
#define RPI_GPIO_PIN_COUNT            54u
#define RPI_GPIO_PINS_PER_SELECT      10u
#define RPI_GPIO_PINS_PER_REGISTER    32u
#define RPI_GPIO_SELECT_BITS          3u
#define RPI_GPIO_SELECT_MASK          7u
#define RPI_GPIO_FUNCTION_INPUT       0u
#define RPI_GPIO_FUNCTION_OUTPUT      1u
#define RPI_GPIO_SELECT_BASE          0x00u
#define RPI_GPIO_SET_BASE             0x1Cu
#define RPI_GPIO_CLEAR_BASE           0x28u
#define RPI_GPIO_LEVEL_BASE           0x34u
#define RPI_GPIO_EVENT_DETECT_STATUS_BASE 0x40u
#define RPI_GPIO_RISING_BASE          0x4Cu
#define RPI_GPIO_FALLING_BASE         0x58u
#define RPI_GPIO_PULL_CONTROL         0x94u
#define RPI_GPIO_PULL_CLOCK_BASE      0x98u
#define RPI_GPIO_PULL_DISABLE_VALUE   0u
#define RPI_GPIO_PULL_DOWN_VALUE      1u
#define RPI_GPIO_PULL_UP_VALUE        2u
#define RPI_GPIO_PULL_SETTLE_LOOPS    150u

static volatile UINT32 *RpiGpioRegister(UINT32 Offset) {
    return (volatile UINT32 *)(UINTN)(RPI_GPIO_BASE + (UINT64)Offset);
}

static void RpiGpioWaitForPull(void) {
    volatile UINT32 Index;

    for (Index = 0u; Index < RPI_GPIO_PULL_SETTLE_LOOPS; Index++) {
        __asm__ volatile("nop");
    }
}

static int RpiGpioCheckPin(UINT32 Pin) {
    return (Pin < RPI_GPIO_PIN_COUNT) ? 0 : -1;
}

UINT32 HalGpioCount(void) {
    return RPI_GPIO_PIN_COUNT;
}

const char *HalGpioBoardName(void) {
    return "rpi-3b";
}

static UINT32 RpiGpioSelectOffset(UINT32 Pin) {
    return RPI_GPIO_SELECT_BASE + (Pin / RPI_GPIO_PINS_PER_SELECT) * sizeof(UINT32);
}

int HalGpioSetMode(UINT32 Pin, HAL_GPIO_MODE Mode) {
    volatile UINT32 *Select;
    UINT32 Shift;
    UINT32 Value;
    UINT32 Function;

    if (RpiGpioCheckPin(Pin) != 0) {
        return -1;
    }
    if (Mode == HAL_GPIO_MODE_INPUT) {
        Function = RPI_GPIO_FUNCTION_INPUT;
    } else if (Mode == HAL_GPIO_MODE_OUTPUT) {
        Function = RPI_GPIO_FUNCTION_OUTPUT;
    } else {
        return -1;
    }
    Select = RpiGpioRegister(RpiGpioSelectOffset(Pin));
    Shift = (Pin % RPI_GPIO_PINS_PER_SELECT) * RPI_GPIO_SELECT_BITS;
    Value = *Select;
    Value &= ~(RPI_GPIO_SELECT_MASK << Shift);
    Value |= (Function & RPI_GPIO_SELECT_MASK) << Shift;
    *Select = Value;
    return 0;
}

int HalGpioSetPull(UINT32 Pin, HAL_GPIO_PULL Pull) {
    volatile UINT32 *Clock;
    UINT32 Value;

    if (RpiGpioCheckPin(Pin) != 0) {
        return -1;
    }
    if (Pull == HAL_GPIO_PULL_NONE) {
        Value = RPI_GPIO_PULL_DISABLE_VALUE;
    } else if (Pull == HAL_GPIO_PULL_DOWN) {
        Value = RPI_GPIO_PULL_DOWN_VALUE;
    } else if (Pull == HAL_GPIO_PULL_UP) {
        Value = RPI_GPIO_PULL_UP_VALUE;
    } else {
        return -1;
    }
    *RpiGpioRegister(RPI_GPIO_PULL_CONTROL) = Value;
    RpiGpioWaitForPull();
    Clock = RpiGpioRegister(RPI_GPIO_PULL_CLOCK_BASE +
                            (Pin / RPI_GPIO_PINS_PER_REGISTER) * sizeof(UINT32));
    *Clock = 1u << (Pin % RPI_GPIO_PINS_PER_REGISTER);
    RpiGpioWaitForPull();
    *RpiGpioRegister(RPI_GPIO_PULL_CONTROL) = RPI_GPIO_PULL_DISABLE_VALUE;
    *Clock = 0u;
    return 0;
}

int HalGpioWrite(UINT32 Pin, int High) {
    UINT32 Offset;

    if (RpiGpioCheckPin(Pin) != 0) {
        return -1;
    }
    Offset = (High != 0 ? RPI_GPIO_SET_BASE : RPI_GPIO_CLEAR_BASE) +
             (Pin / RPI_GPIO_PINS_PER_REGISTER) * sizeof(UINT32);
    *RpiGpioRegister(Offset) = 1u << (Pin % RPI_GPIO_PINS_PER_REGISTER);
    return 0;
}

int HalGpioRead(UINT32 Pin, int *OutHigh) {
    UINT32 Offset;
    UINT32 Value;

    if (OutHigh == 0 || RpiGpioCheckPin(Pin) != 0) {
        return -1;
    }
    Offset = RPI_GPIO_LEVEL_BASE +
             (Pin / RPI_GPIO_PINS_PER_REGISTER) * sizeof(UINT32);
    Value = *RpiGpioRegister(Offset);
    *OutHigh = ((Value >> (Pin % RPI_GPIO_PINS_PER_REGISTER)) & 1u) != 0u ? 1 : 0;
    return 0;
}

static UINT32 RpiGpioBankOffset(UINT32 Pin) {
    return (Pin / RPI_GPIO_PINS_PER_REGISTER) * sizeof(UINT32);
}

static UINT32 RpiGpioPinBit(UINT32 Pin) {
    return 1u << (Pin % RPI_GPIO_PINS_PER_REGISTER);
}

static void RpiGpioClearEventDetect(UINT32 Pin) {
    *RpiGpioRegister(RPI_GPIO_EVENT_DETECT_STATUS_BASE + RpiGpioBankOffset(Pin)) =
        RpiGpioPinBit(Pin);
}

static void RpiGpioWriteEdgeEnableBit(UINT32 Base, UINT32 Pin, int Enable) {
    volatile UINT32 *Register;
    UINT32 Value;
    UINT32 Bit;

    Register = RpiGpioRegister(Base + RpiGpioBankOffset(Pin));
    Bit = RpiGpioPinBit(Pin);
    Value = *Register;
    if (Enable != 0) {
        Value |= Bit;
    } else {
        Value &= ~Bit;
    }
    *Register = Value;
}

int HalGpioEnableEdgeDetect(UINT32 Pin, HAL_GPIO_EDGE Edge) {
    int Rising;
    int Falling;

    if (RpiGpioCheckPin(Pin) != 0 || Edge == HAL_GPIO_EDGE_NONE) {
        return -1;
    }
    Rising = (Edge == HAL_GPIO_EDGE_RISING || Edge == HAL_GPIO_EDGE_BOTH) ? 1 : 0;
    Falling = (Edge == HAL_GPIO_EDGE_FALLING || Edge == HAL_GPIO_EDGE_BOTH) ? 1 : 0;
    RpiGpioWriteEdgeEnableBit(RPI_GPIO_RISING_BASE, Pin, Rising);
    RpiGpioWriteEdgeEnableBit(RPI_GPIO_FALLING_BASE, Pin, Falling);
    RpiGpioClearEventDetect(Pin);
    return 0;
}

int HalGpioDisableEdgeDetect(UINT32 Pin) {
    if (RpiGpioCheckPin(Pin) != 0) {
        return -1;
    }
    RpiGpioWriteEdgeEnableBit(RPI_GPIO_RISING_BASE, Pin, 0);
    RpiGpioWriteEdgeEnableBit(RPI_GPIO_FALLING_BASE, Pin, 0);
    RpiGpioClearEventDetect(Pin);
    return 0;
}

int HalGpioTakeEdgeEvent(UINT32 Pin, int *OutFired) {
    volatile UINT32 *Status;
    UINT32 Bit;

    if (OutFired == 0 || RpiGpioCheckPin(Pin) != 0) {
        return -1;
    }
    Status = RpiGpioRegister(RPI_GPIO_EVENT_DETECT_STATUS_BASE + RpiGpioBankOffset(Pin));
    Bit = RpiGpioPinBit(Pin);
    if ((*Status & Bit) != 0u) {
        *Status = Bit;
        *OutFired = 1;
    } else {
        *OutFired = 0;
    }
    return 0;
}

/*
 * 板图（G4）：只收【确定安全】的脚。
 *   LED → GPIO17（排针 11），BTN → GPIO27（排针 13）：都是常见教学用法，默认不冲突。
 * 刻意避开：
 *   GPIO3  = 电源键（HAL/Arm64/Hal/Hal.c 在用）
 *   GPIO14/15 = UART（早期初始化用）
 * 新加名字前先确认真机上没人用这个脚。
 */
static const HAL_GPIO_BOARD_PIN sRpiBoardPins[] = {
    { "LED", 17u, 11u, HAL_GPIO_MODE_OUTPUT, HAL_GPIO_PULL_NONE },
    { "BTN", 27u, 13u, HAL_GPIO_MODE_INPUT, HAL_GPIO_PULL_UP },
};

const HAL_GPIO_BOARD_PIN *HalGpioBoardPins(UINT32 *OutCount) {
    if (OutCount != 0) {
        *OutCount = (UINT32)(sizeof(sRpiBoardPins) / sizeof(sRpiBoardPins[0]));
    }
    return sRpiBoardPins;
}
