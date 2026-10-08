/*
 * HalVideoStub.c — 视频 HAL：只记配置、不画画
 *
 * 【初学者】
 * 完整 HalVideo 会往帧缓冲写像素、管后缓冲、打字。
 * 当前实现只：
 *   HalVideoSet()            — 记住分辨率 / 帧缓冲地址
 *   HalVideoGetSize() 等     — 供其它代码查询
 *
 * 【积木】门面在 HalVideo.h；本文件是默认薄实现，可换成真画屏后端。
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
