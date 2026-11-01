/*
 * Gpio.c — Milk-V Duo S 的 GPIO（DW APB GPIOA + cv181x pinmux 白名单）
 *
 * 从哪读：
 *   · Synopsys DW APB GPIO：DR +0x00、DDR +0x04、EXT_PORTA +0x50（读【引脚实际电平】）
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
 */
#include "HalGpio.h"

#define DUO_GPIOA_BASE 0x03020000ULL
#define DUO_PINMUX_BASE 0x03001000ULL
#define DUO_GPIO_DATA 0x00u
#define DUO_GPIO_DIRECTION 0x04u
#define DUO_GPIO_EXTERNAL_PORT 0x50u
#define DUO_GPIOA_BANK_WIDTH 32u
#define DUO_PINMUX_FUNCTION_MASK 7u
#define DUO_PINMUX_FUNCTION_XGPIOA 3u

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
