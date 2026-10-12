/*
 * Video.c — InitializeVideo（开机模块编织）
 *
 * 人话：帧缓冲、字体、主题和核显/声卡探针。无卡则软退。勿全屏 Clear。
 */
#include "KernelModulesPrivate.h"
#include "BootInfo.h"
#include "Hal.h"
#include "Font.h"
#include "Theme.h"

static void IgpuAudioProbe(void) {
    HalIgpuMmioInitialize();
    HalHdaMmioInitialize();
    HalHdaCodecInitialize();
    HalIgpuForcewakeInitialize();
    HalHdaStreamInitialize();
    HalIgpuGttInitialize();
    HalIgpuBlitInitialize();
    HalIgpuPresentPrepare();
}

int InitializeVideo(void) {
    const BOOT_INFO *Info = BootInfoGet();
    VIDEO_CONFIG Video = BootInfoToVideoConfig(Info);

    FontInitialize();
    ThemeInitialize();
    HalVideoSet(&Video);
    HalVideoEnableFbWc();
    HalVideoInitializeBackbuffer();
    HalSerialGopEnable();
    HalVideoLogFbPte();
    IgpuAudioProbe();
    return 0;
}
