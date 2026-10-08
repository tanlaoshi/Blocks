/*
 * HalVideoStub.c — Hal/Common：视频 HAL 默认薄实现（只记配置、不画画）
 *
 * 【初学者】
 * Arm/RiscV 先链本文件；X64 链 Hal/X64/HalVideo.c（真画点）。
 * 门面仍在 Include/Hal/HalVideo.h。
 */
#include "HalVideo.h"

static VIDEO_CONFIG gVideo;
static int gVideoValid;

void HalVideoSet(const VIDEO_CONFIG *Config) {
    if (Config == 0) {
        gVideoValid = 0;
        return;
    }
    gVideo = *Config;
    gVideoValid = 1;
}

void HalVideoGetSize(UINT32 *Width, UINT32 *Height) {
    if (Width) {
        *Width = gVideoValid ? gVideo.HorizontalResolution : 0;
    }
    if (Height) {
        *Height = gVideoValid ? gVideo.VerticalResolution : 0;
    }
}

UINT64 HalVideoFrameBufferBase(void) {
    return gVideoValid ? gVideo.FrameBufferBase : 0;
}

UINT64 HalVideoFrameBufferSize(void) {
    return gVideoValid ? gVideo.FrameBufferSize : 0;
}

/* Arm64/RiscV 本刀仍为空：有配置但不画像素（真画屏另刀） */
void HalVideoDrawPixel(UINT32 X, UINT32 Y, UINT32 Color) {
    (void)X;
    (void)Y;
    (void)Color;
}

void HalVideoFillRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height,
                      UINT32 Color) {
    (void)X;
    (void)Y;
    (void)Width;
    (void)Height;
    (void)Color;
}
