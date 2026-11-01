/*
 * GpioDefault.c — 没有 GPIO 的板子（x64 等）的弱实现
 *
 * 为什么需要它：
 *   命令层（Services/Shell/Commands/Gpio.c）是三架构共编的，必须能链接到 HalGpio*。
 *   有 GPIO 的板子在 HAL/<Arch>/Board/<board>/Gpio/Gpio.c 里给【强符号】，
 *   链接器会盖掉这里；没有 GPIO 的板子就落到这里，返回 -1（"不支持"），
 *   而不是链接失败、也不是假装成功。
 *
 * 别改：这里必须是 __attribute__((weak))，否则会与板级实现重复定义。
 */
#include "HalGpio.h"

__attribute__((weak)) UINT32 HalGpioCount(void) {
    return 0u;
}

__attribute__((weak)) const char *HalGpioBoardName(void) {
    return 0;
}

__attribute__((weak)) int HalGpioSetMode(UINT32 Pin, HAL_GPIO_MODE Mode) {
    (void)Pin;
    (void)Mode;
    return -1;
}

__attribute__((weak)) int HalGpioSetPull(UINT32 Pin, HAL_GPIO_PULL Pull) {
    (void)Pin;
    (void)Pull;
    return -1;
}

__attribute__((weak)) int HalGpioWrite(UINT32 Pin, int High) {
    (void)Pin;
    (void)High;
    return -1;
}

__attribute__((weak)) int HalGpioRead(UINT32 Pin, int *OutHigh) {
    (void)Pin;
    (void)OutHigh;
    return -1;
}

__attribute__((weak)) int HalGpioEnableEdgeDetect(UINT32 Pin, HAL_GPIO_EDGE Edge) {
    (void)Pin;
    (void)Edge;
    return -1;
}

__attribute__((weak)) int HalGpioDisableEdgeDetect(UINT32 Pin) {
    (void)Pin;
    return -1;
}

__attribute__((weak)) int HalGpioTakeEdgeEvent(UINT32 Pin, int *OutFired) {
    (void)Pin;
    (void)OutFired;
    return -1;
}

__attribute__((weak)) int HalGpioEdgeSnapshot(UINT32 Pin, UINT32 Out[12]) {
    (void)Pin;
    (void)Out;
    return -1;
}
