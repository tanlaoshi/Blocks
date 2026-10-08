/*
 * SmpStub.c — RiscV SecondaryPark 所需符号桩（完整 SMP 未迁入）
 *
 * Boot.S 次 hart 会轮询 gApGo；本刀永不写 magic，次核停在 park。
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
