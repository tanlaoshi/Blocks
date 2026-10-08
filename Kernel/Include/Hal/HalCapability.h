/*
 * HalCapability.h — 「这块板子有什么能力？」
 *
 * 【分层】本文件不依赖 Core/BootInfo 仓储。
 * KernelMain 在 BootInfoStore 之后调用 HalCapabilityObserveFrameBuffer，
 * 把「有没有 FB」注入 Hal；之后模块只问 HalHasFrameBuffer() 等。
 */
#ifndef HAL_CAPABILITY_H
#define HAL_CAPABILITY_H

#include "BootTypes.h"

/* Core → Hal：注入帧缓冲是否存在（FbSize!=0 视为有屏） */
void HalCapabilityObserveFrameBuffer(UINT64 FrameBufferSize);

int HalHasFrameBuffer(void);
int HalConsoleOnly(void);
int HalPlatformIsVirtSerialConsole(void);
void HalCpuPark(void);

#endif
