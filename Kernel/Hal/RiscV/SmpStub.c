/*
 * SmpStub.c — RiscV 次核（AP）所需符号的空桩
 *
 * 【初学者】
 * KernelEntry.S 里，没抢到 BSP 的 hart 会进 SecondaryPark，轮询数组 gApGo[]。
 * 多核就绪后，BSP 会写入启动魔数 / 逻辑 CPU 号 / 栈顶，并跳进 HalApMain。
 * 当前：这些全局存在，但不写启动魔数 → 次核一直 park。
 * 单核运行时不会走到次核路径。
 */
#include "BootTypes.h"

#define HAL_MAX_CPUS 16

volatile UINT32 gApGo[HAL_MAX_CPUS];
UINT32 gApLogical[HAL_MAX_CPUS];
UINT64 gApStackTop[HAL_MAX_CPUS];

void HalApMain(UINT32 LogicalCpu) {
    (void)LogicalCpu;
    for (;;) {
    }
}
