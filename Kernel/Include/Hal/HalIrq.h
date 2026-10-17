/*
 * HalIrq.h — 开中断、保存现场、向量登记
 */
#ifndef HAL_IRQ_H
#define HAL_IRQ_H

#include "BootTypes.h"

void HalIrqEnable(void);
void HalIrqDisable(void);
/* PR-A5：保存并关中断；Restore 恢复进入时的中断状态（可嵌套调用方自管） */
UINT64 HalIrqSave(void);
void HalIrqRestore(UINT64 Flags);
/* Type：架构相关门描述（x86 为 IDT type；其它架构可忽略） */
void HalIrqVectorSet(UINT32 Vector, void *Handler, UINT8 Type);
void HalIrqRegister(UINT32 Vector, void (*Handler)(void));
void HalIrqUnregister(UINT32 Vector);
void HalIrqEoi(UINT32 Vector);

#endif
