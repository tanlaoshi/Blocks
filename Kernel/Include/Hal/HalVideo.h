/*
 * HalVideo.h — 帧缓冲 / 画点 HAL 门面（仅已实现 API）
 *
 * 【初学者】
 * Set / GetSize / DrawPixel / FillRect / 背缓冲 Present。
 * 画字不在本头：桌面见 Font.h；开机屏见 HalBootFont（HAL 内）。
 * 禁止把未实现的现网 API 预先堆进本头。
 */
#ifndef HAL_VIDEO_H
#define HAL_VIDEO_H

#include "BootInfoTypes.h"

void HalVideoSet(const VIDEO_CONFIG *Config);
void HalVideoGetSize(UINT32 *Width, UINT32 *Height);
UINT64 HalVideoFrameBufferBase(void);
UINT64 HalVideoFrameBufferSize(void);

void HalVideoInitializeBackbuffer(void);
int HalVideoBackbufferEnabled(void);
UINT64 HalVideoBackbufferBase(void);
void HalVideoPresent(void);
void HalVideoPresentFlush(void);

void HalVideoDrawPixel(UINT32 X, UINT32 Y, UINT32 Color);
UINT32 HalVideoReadPixel(UINT32 X, UINT32 Y);
void HalVideoXorPixelRaw(UINT32 X, UINT32 Y, UINT32 Mask);
void HalVideoFillRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color);

#endif
