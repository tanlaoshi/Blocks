/*
 * HalDma.h — HAL 驱动取连续物理页（DMA / virtqueue）
 *
 * 【初学者】
 * 驱动只认本门面，不 include Core/PhysicalMemory.h。
 * 符号由 PMM（Core）提供：Hal → 契约，Core → 实现。
 */
#ifndef HAL_DMA_H
#define HAL_DMA_H

#include "BootTypes.h"

/* 连续 Count 个 4KiB 页；失败返回 0 */
void *HalDmaAllocatePages(UINT32 Count);

#endif
