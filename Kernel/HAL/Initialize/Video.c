/*
 * Video.c — InitializeVideo（开机模块表；Hal 本层）
 *
 * 帧缓冲、字体、主题和核显/声卡探针。勿全屏 Clear。
 */
#include "KernelModulesPrivate.h"
#include "BootInfo.h"
#include "Hal.h"
#include "Font.h"
#include "Theme.h"
#include "Gfx.h"

int DisplayHardwareInitialize(void) {
    int Found = HalIgpuMmioInitialize();

    HalHdaMmioInitialize();
    HalHdaCodecInitialize();
    HalIgpuForcewakeInitialize();
    HalHdaStreamInitialize();
    HalIgpuGttInitialize();
    HalIgpuBlitInitialize();
    HalIgpuPresentPrepare();
    return Found;
}

int InitializeVideo(void) {
    const BOOT_INFO *Info = BootInfoGet();
    VIDEO_CONFIG Video = BootInfoToVideoConfig(Info);

    FontInitialize();
    ThemeInitialize();
    HalVideoSet(&Video);
    HalVideoEnableFbWc();
    HalVideoInitializeBackbuffer();
    GfxSetPixel(HalVideoDrawPixel);
    GfxSetFillRect(HalVideoFillRect);
    HalSerialGopEnable();
    HalVideoLogFbPte();
    (void)DisplayHardwareInitialize();
    return 0;
}
