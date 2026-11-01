/*
 * HalGpio.h — 通用 GPIO 契约（三架构共用，板级实现）
 *
 * 谁实现：HAL/<Arch>/Board/<board>/Gpio/Gpio.c（强符号）
 *         没实现的板子落到 HAL/Common/Gpio/GpioDefault.c（弱符号，一律返回 -1）
 * 谁用：   Services/Shell/Commands/Gpio.c，以及将来的按键/LED/复位脚驱动
 *
 * 约定：
 *   · Pin 是【本板物理引脚号】（Pi 是 BCM 编号 0..53；Duo 是 GPIOA 位号）；
 *     逻辑名（LED/BTN）由板级表负责，不在本契约里。
 *   · 返回 0 = 成功，非 0 = 不支持/参数错。绝不在失败时静默"当作成功"。
 *   · 读回的是【引脚实际电平】（不是输出寄存器），所以输出模式下可用于自检。
 */
#ifndef HAL_GPIO_H
#define HAL_GPIO_H

#include "BootTypes.h"

typedef enum {
    HAL_GPIO_MODE_INPUT = 0,
    HAL_GPIO_MODE_OUTPUT = 1
} HAL_GPIO_MODE;

typedef enum {
    HAL_GPIO_PULL_NONE = 0,
    HAL_GPIO_PULL_UP = 1,
    HAL_GPIO_PULL_DOWN = 2
} HAL_GPIO_PULL;

/*
 * 边沿检测（G5）：硬件锁存事件，软件轮询取走。
 *   不依赖 CPU 中断框架；真·IRQ 进 GIC/PLIC 另开一刀。
 *   RISING / FALLING / BOTH 与 BCM2835 §6、DW APB GPIO 边沿语义对齐。
 */
typedef enum {
    HAL_GPIO_EDGE_NONE = 0,
    HAL_GPIO_EDGE_RISING = 1,
    HAL_GPIO_EDGE_FALLING = 2,
    HAL_GPIO_EDGE_BOTH = 3
} HAL_GPIO_EDGE;

/*
 * 板图里的一条：逻辑名 → 物理引脚。
 *   HeaderPin 是排针物理脚号（0 = 没有这个概念），供人对照丝印；
 *   命令里既可以用 Name，也可以直接用引脚号。
 */
typedef struct {
    const char *Name;
    UINT32 Pin;
    UINT32 HeaderPin;
    HAL_GPIO_MODE DefaultMode;
    HAL_GPIO_PULL DefaultPull;
} HAL_GPIO_BOARD_PIN;

/* 本板可用引脚数；0 = 本板没有 GPIO（x64 等） */
UINT32 HalGpioCount(void);

/* 板名（"rpi-3b"/"milk-v-duo-s"），用于日志；没有实现返回 0 */
const char *HalGpioBoardName(void);

int HalGpioSetMode(UINT32 Pin, HAL_GPIO_MODE Mode);
int HalGpioSetPull(UINT32 Pin, HAL_GPIO_PULL Pull);
int HalGpioWrite(UINT32 Pin, int High);
/* 读实际电平：0 = 成功并写 *OutHigh；非 0 = 失败 */
int HalGpioRead(UINT32 Pin, int *OutHigh);

/*
 * 边沿：Enable 打开硬件检测并清掉陈旧事件；Disable 关掉检测并再清一次。
 * TakeEdgeEvent：若锁存了事件则清掉并 *OutFired=1，否则 *OutFired=0；返回 0=调用成功。
 */
int HalGpioEnableEdgeDetect(UINT32 Pin, HAL_GPIO_EDGE Edge);
int HalGpioDisableEdgeDetect(UINT32 Pin);
int HalGpioTakeEdgeEvent(UINT32 Pin, int *OutFired);

/* 本板板图（弱实现返回 0 条） */
const HAL_GPIO_BOARD_PIN *HalGpioBoardPins(UINT32 *OutCount);

/* 名字 → 引脚号（大小写不敏感）；找不到返回 -1。实现在 HAL/Common/Gpio */
int HalGpioFindPin(const char *Name, UINT32 *OutPin);

/* 按板图应用默认模式/上下拉（纯数字没有默认值，返回 -1） */
int HalGpioApplyNamedDefault(const char *Name, UINT32 *OutPin);

#endif
