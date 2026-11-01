/*
 * GpioName.c — 板图(名字→引脚)的通用部分：弱默认表 + 名字解析
 *
 * 为什么放 Common：
 *   每个板子只该提供【一张表】（HAL/<Arch>/Board/<board>/Gpio/Gpio.c 里的强符号
 *   HalGpioBoardPins），而"大小写不敏感匹配""名字/数字二选一"这些逻辑三架构一样，
 *   放这里一次，板级实现里就不许再写第二份。
 *
 * 别改：
 *   · 名字比较必须大小写不敏感（用户会输 led/LED/Led）；
 *   · 解析失败必须返回 -1，不许退化成 0 号脚 —— 那会让人以为成功了。
 */
#include "HalGpio.h"

/* 没有板图的板子（x64、以及还没做 G3 的 Duo） */
__attribute__((weak)) const HAL_GPIO_BOARD_PIN *HalGpioBoardPins(UINT32 *OutCount) {
    if (OutCount != 0) {
        *OutCount = 0u;
    }
    return 0;
}

static char GpioLower(char Value) {
    if (Value >= 'A' && Value <= 'Z') {
        return (char)(Value - 'A' + 'a');
    }
    return Value;
}

static int GpioNameEqual(const char *Left, const char *Right) {
    while (*Left != 0 && *Right != 0) {
        if (GpioLower(*Left) != GpioLower(*Right)) {
            return 0;
        }
        Left++;
        Right++;
    }
    return (*Left == 0 && *Right == 0) ? 1 : 0;
}

int HalGpioFindPin(const char *Name, UINT32 *OutPin) {
    const HAL_GPIO_BOARD_PIN *Pins;
    UINT32 Count = 0u;
    UINT32 Index;

    if (Name == 0 || OutPin == 0 || Name[0] == 0) {
        return -1;
    }
    Pins = HalGpioBoardPins(&Count);
    if (Pins == 0) {
        return -1;
    }
    for (Index = 0u; Index < Count; Index++) {
        if (Pins[Index].Name != 0 && GpioNameEqual(Pins[Index].Name, Name)) {
            *OutPin = Pins[Index].Pin;
            return 0;
        }
    }
    return -1;
}
