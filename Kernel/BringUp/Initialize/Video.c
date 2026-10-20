/*
 * Video.c — VideoInitialize（开机模块表；BringUp 胶水）
 *
 * 帧缓冲、字体、主题和核显/声卡探针。勿全屏 Clear。
 */
#include "KernelModulesPrivate.h"
#include "BootInfo.h"
#include "Hal.h"
#include "Font.h"
#include "Theme.h"
#include "Graphics.h"
#include "Desktop.h"

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

int VideoInitialize(void) {
    const BOOT_INFO *Info = BootInfoGet();
    VIDEO_CONFIG Video = BootInfoToVideoConfig(Info);

    FontInitialize();
    ThemeInitialize();
    HalVideoSet(&Video);
    HalVideoEnableFbWc();
    HalVideoInitializeBackbuffer();
    GraphicsSetPixel(HalVideoDrawPixel);
    GraphicsSetFillRect(HalVideoFillRect);
    HalSerialGopEnable();
    HalVideoLogFbPte();
    /*
     * 屏：Boot 已 GOP 铺黑，这里绝不 Present 底色（逐行写 LFB 会撕）。
     * 后缓冲：PMM 页是未初始化的，须铺黑并 DiscardDirty——否则之后 igpu
     * 整帧翻页会把垃圾像素拷上屏。这是填内存，不是再画一遍显示器。
     */
    HalVideoClearScreen(0x00000000u);
    HalVideoDiscardDirty();
    (void)DisplayHardwareInitialize();
    DesktopBootLogoShow();
    return 0;
}
