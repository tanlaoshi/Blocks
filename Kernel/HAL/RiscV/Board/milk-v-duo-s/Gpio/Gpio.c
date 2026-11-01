/*
 * Gpio.c — Milk-V Duo S 的 GPIO（DW APB GPIOA + cv181x pinmux 白名单）
 *
 * 从哪读：
 *   · Synopsys DW APB GPIO：DR +0x00、DDR +0x04、EXT_PORTA +0x50（读【引脚实际电平】）
 *   · 边沿（G5）：INTEN +0x30、INTMASK +0x34、INTTYPE_LEVEL +0x38、
 *     INT_POLARITY +0x3C、INTSTATUS +0x40、PORTA_EOI +0x4C（写 1 清）
 *   · pinmux：`cv181x_reg_fmux_gpio.h` + `cv181x_pinlist_swconfig.h`（FUNC=3 → XGPIOA）
 *   · 基址：GPIOA `0x03020000`、FMUX `0x03001000`（与 UsbHostController/Sdhci 一致）
 *
 * 白名单（先 3 个安全脚，G3 红线）：
 *   GPIOA18 = JTAG_CPU_TCK  pad（fmux +0x68）
 *   GPIOA19 = JTAG_CPU_TMS  pad（fmux +0x64）
 *   GPIOA20 = JTAG_CPU_TRST pad（fmux +0x6c）
 * 刻意避开：GPIOA15（WiFi/SPK 上电）、16/17（UART0）、30（USB PORT_SEL）、SD0 脚。
 *
 * 别改：读回一定走 EXT_PORTA；非白名单一律 -1（防改串口/SD 失联）。
 *       BOTH 边沿照 Linux gpio-dwapb：每次取走后按当前电平翻转极性（控制器一次只盯一边）。
 */
#include "HalGpio.h"

#define DUO_GPIOA_BASE 0x03020000ULL
#define DUO_PINMUX_BASE 0x03001000ULL
#define DUO_GPIO_DATA 0x00u
#define DUO_GPIO_DIRECTION 0x04u
#define DUO_GPIO_INTERRUPT_ENABLE 0x30u
#define DUO_GPIO_INTERRUPT_MASK 0x34u
#define DUO_GPIO_INTERRUPT_TYPE_LEVEL 0x38u
#define DUO_GPIO_INTERRUPT_POLARITY 0x3Cu
#define DUO_GPIO_INTERRUPT_STATUS 0x40u
#define DUO_GPIO_RAW_INTERRUPT_STATUS 0x44u /* Synopsys：未掩码锁存 */
#define DUO_GPIO_DEBOUNCE 0x48u
#define DUO_GPIO_PORT_A_END_OF_INTERRUPT 0x4Cu
#define DUO_GPIO_EXTERNAL_PORT 0x50u
/* LS_SYNC@0x60 本硅写后读回 0（无此功能或布局不同）；勿再当必配 */
#define DUO_GPIOA_BANK_WIDTH 32u
#define DUO_PINMUX_FUNCTION_MASK 7u
#define DUO_PINMUX_FUNCTION_XGPIOA 3u
#define DUO_GPIO_INTERRUPT_TYPE_EDGE 1u

/* cv181x_reg_fmux_gpio.h */
#define DUO_FMUX_JTAG_CPU_TMS 0x64u
#define DUO_FMUX_JTAG_CPU_TCK 0x68u
#define DUO_FMUX_JTAG_CPU_TRST 0x6cu

typedef struct {
    UINT32 Pin;
    UINT32 PinmuxOffset;
} DUO_GPIO_ALLOW;

static const DUO_GPIO_ALLOW sDuoGpioAllow[] = {
    {18u, DUO_FMUX_JTAG_CPU_TCK},
    {19u, DUO_FMUX_JTAG_CPU_TMS},
    {20u, DUO_FMUX_JTAG_CPU_TRST},
};

#define DUO_GPIO_ALLOW_COUNT \
    ((UINT32)(sizeof(sDuoGpioAllow) / sizeof(sDuoGpioAllow[0])))

static volatile UINT32 *DuoGpioRegister(UINT32 Offset) {
    return (volatile UINT32 *)(UINTN)(DUO_GPIOA_BASE + (UINT64)Offset);
}

static volatile UINT32 *DuoPinmuxRegister(UINT32 Offset) {
    return (volatile UINT32 *)(UINTN)(DUO_PINMUX_BASE + (UINT64)Offset);
}

static const DUO_GPIO_ALLOW *DuoGpioFindAllow(UINT32 Pin) {
    UINT32 Index;

    for (Index = 0u; Index < DUO_GPIO_ALLOW_COUNT; Index++) {
        if (sDuoGpioAllow[Index].Pin == Pin) {
            return &sDuoGpioAllow[Index];
        }
    }
    return 0;
}

static int DuoGpioSelectAsGpio(UINT32 Pin) {
    const DUO_GPIO_ALLOW *Allow;
    volatile UINT32 *Control;
    UINT32 Value;

    Allow = DuoGpioFindAllow(Pin);
    if (Allow == 0) {
        return -1;
    }
    Control = DuoPinmuxRegister(Allow->PinmuxOffset);
    Value = *Control;
    Value = (Value & ~DUO_PINMUX_FUNCTION_MASK) | DUO_PINMUX_FUNCTION_XGPIOA;
    *Control = Value;
    return 0;
}

UINT32 HalGpioCount(void) {
    /* 数字参数上界：GPIOA 位宽；真正可操作集合仍是白名单 */
    return DUO_GPIOA_BANK_WIDTH;
}

const char *HalGpioBoardName(void) {
    return "milk-v-duo-s";
}

int HalGpioSetMode(UINT32 Pin, HAL_GPIO_MODE Mode) {
    volatile UINT32 *Direction;
    UINT32 Value;
    UINT32 Bit;

    if (DuoGpioSelectAsGpio(Pin) != 0) {
        return -1;
    }
    if (Mode != HAL_GPIO_MODE_INPUT && Mode != HAL_GPIO_MODE_OUTPUT) {
        return -1;
    }
    Bit = 1u << Pin;
    Direction = DuoGpioRegister(DUO_GPIO_DIRECTION);
    Value = *Direction;
    if (Mode == HAL_GPIO_MODE_OUTPUT) {
        Value |= Bit;
    } else {
        Value &= ~Bit;
    }
    *Direction = Value;
    return 0;
}

/*
 * 上下拉在 cv181x 另有 IOCFG 寄存器；本刀只认 NONE，其余拒绝（不假装成功）。
 */
int HalGpioSetPull(UINT32 Pin, HAL_GPIO_PULL Pull) {
    if (DuoGpioFindAllow(Pin) == 0) {
        return -1;
    }
    if (Pull == HAL_GPIO_PULL_NONE) {
        return 0;
    }
    return -1;
}

int HalGpioWrite(UINT32 Pin, int High) {
    volatile UINT32 *Data;
    UINT32 Value;
    UINT32 Bit;

    if (DuoGpioFindAllow(Pin) == 0) {
        return -1;
    }
    Bit = 1u << Pin;
    Data = DuoGpioRegister(DUO_GPIO_DATA);
    Value = *Data;
    if (High != 0) {
        Value |= Bit;
    } else {
        Value &= ~Bit;
    }
    *Data = Value;
    return 0;
}

int HalGpioRead(UINT32 Pin, int *OutHigh) {
    UINT32 Value;

    if (OutHigh == 0 || DuoGpioFindAllow(Pin) == 0) {
        return -1;
    }
    Value = *DuoGpioRegister(DUO_GPIO_EXTERNAL_PORT);
    *OutHigh = ((Value >> Pin) & 1u) != 0u ? 1 : 0;
    return 0;
}

/* 记住 BOTH：Take 后要按当前电平翻极性（见文件头注释） */
static HAL_GPIO_EDGE sDuoEdgeMode[DUO_GPIOA_BANK_WIDTH];

static void DuoGpioWriteBit(UINT32 Offset, UINT32 Pin, int Set) {
    volatile UINT32 *Register;
    UINT32 Value;
    UINT32 Bit;

    Register = DuoGpioRegister(Offset);
    Bit = 1u << Pin;
    Value = *Register;
    if (Set != 0) {
        Value |= Bit;
    } else {
        Value &= ~Bit;
    }
    *Register = Value;
}

static void DuoGpioClearEndOfInterrupt(UINT32 Pin) {
    *DuoGpioRegister(DUO_GPIO_PORT_A_END_OF_INTERRUPT) = 1u << Pin;
}

static void DuoGpioSetPolarityForNextEdge(UINT32 Pin) {
    UINT32 Level;
    int High;

    Level = *DuoGpioRegister(DUO_GPIO_EXTERNAL_PORT);
    High = ((Level >> Pin) & 1u) != 0u ? 1 : 0;
    /* 当前为高 → 等下降；当前为低 → 等上升（Linux dwapb_toggle_trigger） */
    DuoGpioWriteBit(DUO_GPIO_INTERRUPT_POLARITY, Pin, High == 0 ? 1 : 0);
}

int HalGpioEnableEdgeDetect(UINT32 Pin, HAL_GPIO_EDGE Edge) {
    if (DuoGpioFindAllow(Pin) == 0 || Edge == HAL_GPIO_EDGE_NONE ||
        Pin >= DUO_GPIOA_BANK_WIDTH) {
        return -1;
    }
    sDuoEdgeMode[Pin] = Edge;
    DuoGpioWriteBit(DUO_GPIO_INTERRUPT_TYPE_LEVEL, Pin, (int)DUO_GPIO_INTERRUPT_TYPE_EDGE);
    if (Edge == HAL_GPIO_EDGE_RISING) {
        DuoGpioWriteBit(DUO_GPIO_INTERRUPT_POLARITY, Pin, 1);
    } else if (Edge == HAL_GPIO_EDGE_FALLING) {
        DuoGpioWriteBit(DUO_GPIO_INTERRUPT_POLARITY, Pin, 0);
    } else {
        DuoGpioSetPolarityForNextEdge(Pin);
    }
    /* 去抖关掉：否则短翻转可能被吃掉 */
    DuoGpioWriteBit(DUO_GPIO_DEBOUNCE, Pin, 0);
    DuoGpioWriteBit(DUO_GPIO_INTERRUPT_MASK, Pin, 0);
    DuoGpioClearEndOfInterrupt(Pin);
    DuoGpioWriteBit(DUO_GPIO_INTERRUPT_ENABLE, Pin, 1);
    return 0;
}

int HalGpioDisableEdgeDetect(UINT32 Pin) {
    if (DuoGpioFindAllow(Pin) == 0 || Pin >= DUO_GPIOA_BANK_WIDTH) {
        return -1;
    }
    DuoGpioWriteBit(DUO_GPIO_INTERRUPT_ENABLE, Pin, 0);
    DuoGpioWriteBit(DUO_GPIO_INTERRUPT_MASK, Pin, 1);
    DuoGpioClearEndOfInterrupt(Pin);
    sDuoEdgeMode[Pin] = HAL_GPIO_EDGE_NONE;
    return 0;
}

int HalGpioTakeEdgeEvent(UINT32 Pin, int *OutFired) {
    UINT32 Status;
    UINT32 RawStatus;
    UINT32 Bit;

    if (OutFired == 0 || DuoGpioFindAllow(Pin) == 0 || Pin >= DUO_GPIOA_BANK_WIDTH) {
        return -1;
    }
    Bit = 1u << Pin;
    Status = *DuoGpioRegister(DUO_GPIO_INTERRUPT_STATUS);
    RawStatus = *DuoGpioRegister(DUO_GPIO_RAW_INTERRUPT_STATUS);
    if (((Status | RawStatus) & Bit) != 0u) {
        DuoGpioClearEndOfInterrupt(Pin);
        if (sDuoEdgeMode[Pin] == HAL_GPIO_EDGE_BOTH) {
            DuoGpioSetPolarityForNextEdge(Pin);
        }
        *OutFired = 1;
    } else {
        *OutFired = 0;
    }
    return 0;
}

/*
 * 板图（G3 最小集）：名字 = GPIOA 位号。HeaderPin=0（丝印对照后补）。
 */
static const HAL_GPIO_BOARD_PIN sDuoBoardPins[] = {
    {"GP18", 18u, 0u, HAL_GPIO_MODE_OUTPUT, HAL_GPIO_PULL_NONE},
    {"GP19", 19u, 0u, HAL_GPIO_MODE_OUTPUT, HAL_GPIO_PULL_NONE},
    {"GP20", 20u, 0u, HAL_GPIO_MODE_OUTPUT, HAL_GPIO_PULL_NONE},
};

const HAL_GPIO_BOARD_PIN *HalGpioBoardPins(UINT32 *OutCount) {
    if (OutCount != 0) {
        *OutCount = (UINT32)(sizeof(sDuoBoardPins) / sizeof(sDuoBoardPins[0]));
    }
    return sDuoBoardPins;
}
