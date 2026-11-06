/*
 * LocalInterruptController.h — BCM2836/7 ARM-local intc（psmp-3a）
 *
 * 物理基址 0x40000000（外设窗 0x3F000000 之上）。禁 virt GIC@0x08000000。
 */
#ifndef LOCAL_INTERRUPT_CONTROLLER_H
#define LOCAL_INTERRUPT_CONTROLLER_H

#include "BootTypes.h"

void HalLocalInterruptControllerInitialize(void);
void HalLocalInterruptControllerInitializeCpu(void);
UINT32 HalLocalInterruptControllerAcknowledge(void);
void HalLocalInterruptControllerEndOfInterrupt(UINT32 InterruptId);
int HalLocalInterruptControllerIsTimer(UINT32 InterruptId);

#endif /* LOCAL_INTERRUPT_CONTROLLER_H */
