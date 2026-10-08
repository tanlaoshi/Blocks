/*
 * HalCapability.h — 「这块板子有什么能力？」
 *
 * 【初学者】
 * Common 代码不要写 #ifdef __x86_64__ 来决定走桌面还是串口壳。
 * 应问 Hal：
 *   HalHasFrameBuffer()              — 有没有可用帧缓冲？
 *   HalConsoleOnly()                 — 是否只能串口命令行？
 *   HalPlatformIsVirtSerialConsole() — 是不是 virt 那类平台形状？
 *
 * KernelMain 用这三面旗标选模块表。
 * HalCpuPark()：CPU 停车（省电空转），失败或收尾路径会用到。
 *
 * 【积木】门面稳定；取值逻辑见 HalCapability.c。
 */
#ifndef HAL_CAPABILITY_H
#define HAL_CAPABILITY_H

int HalHasFrameBuffer(void);
int HalConsoleOnly(void);
int HalPlatformIsVirtSerialConsole(void);
void HalCpuPark(void);

#endif
