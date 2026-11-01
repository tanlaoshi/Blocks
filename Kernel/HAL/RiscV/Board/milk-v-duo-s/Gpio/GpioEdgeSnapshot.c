/*
 * GpioEdgeSnapshot.c — Duo G5 边沿寄存器现场（edgeselftest 失败诊断）
 */
#include "HalGpio.h"

#define DUO_GPIOA_BASE 0x03020000ULL
#define DUO_GPIO_DATA 0x00u
#define DUO_GPIO_DIRECTION 0x04u
#define DUO_GPIO_INTERRUPT_ENABLE 0x30u
#define DUO_GPIO_INTERRUPT_MASK 0x34u
#define DUO_GPIO_INTERRUPT_TYPE_LEVEL 0x38u
#define DUO_GPIO_INTERRUPT_POLARITY 0x3Cu
#define DUO_GPIO_INTERRUPT_STATUS 0x40u
#define DUO_GPIO_RAW_INTERRUPT_STATUS 0x44u
#define DUO_GPIO_DEBOUNCE 0x48u
#define DUO_GPIO_EXTERNAL_PORT 0x50u
#define DUO_GPIO_LEVEL_SYNC 0x60u
#define DUO_GPIOA_BANK_WIDTH 32u

/* 与 Gpio.c 白名单一致：仅诊断已允许脚 */
static int DuoGpioSnapshotPinAllowed(UINT32 Pin) {
    return (Pin == 18u || Pin == 19u || Pin == 20u) ? 1 : 0;
}

static volatile UINT32 *DuoGpioSnapshotRegister(UINT32 Offset) {
    return (volatile UINT32 *)(UINTN)(DUO_GPIOA_BASE + (UINT64)Offset);
}

int HalGpioEdgeSnapshot(UINT32 Pin, UINT32 Out[12]) {
    if (Out == 0 || DuoGpioSnapshotPinAllowed(Pin) == 0 ||
        Pin >= DUO_GPIOA_BANK_WIDTH) {
        return -1;
    }
    Out[0] = *DuoGpioSnapshotRegister(DUO_GPIO_DATA);
    Out[1] = *DuoGpioSnapshotRegister(DUO_GPIO_DIRECTION);
    Out[2] = *DuoGpioSnapshotRegister(DUO_GPIO_INTERRUPT_ENABLE);
    Out[3] = *DuoGpioSnapshotRegister(DUO_GPIO_INTERRUPT_MASK);
    Out[4] = *DuoGpioSnapshotRegister(DUO_GPIO_INTERRUPT_TYPE_LEVEL);
    Out[5] = *DuoGpioSnapshotRegister(DUO_GPIO_INTERRUPT_POLARITY);
    Out[6] = *DuoGpioSnapshotRegister(DUO_GPIO_INTERRUPT_STATUS);
    Out[7] = *DuoGpioSnapshotRegister(DUO_GPIO_RAW_INTERRUPT_STATUS);
    Out[8] = *DuoGpioSnapshotRegister(DUO_GPIO_DEBOUNCE);
    Out[9] = *DuoGpioSnapshotRegister(DUO_GPIO_EXTERNAL_PORT);
    Out[10] = *DuoGpioSnapshotRegister(DUO_GPIO_LEVEL_SYNC);
    Out[11] = Pin;
    return 0;
}
