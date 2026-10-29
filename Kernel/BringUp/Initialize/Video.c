/*
 * Video.c — VideoInitialize（开机模块表；BringUp 胶水）
 *
 * 帧缓冲、字体、主题和核显探针（HDA 见 boot-hda-defer / Worker）。勿全屏 Clear。
 */
#include "KernelModulesPrivate.h"
#include "BootHandoff.h"
#include "Hal.h"
#include "Font.h"
#include "Theme.h"
#include "Graphics.h"
#include "Desktop.h"

int DisplayHardwareInitialize(void) {
    int Found = HalIgpuMmioInitialize();

    /* boot-hda-defer：HDA 桌面后 Worker 再探针；igpu 留开机服务 Present */
    HalIgpuForcewakeInitialize();
    HalIgpuGttInitialize();
    HalIgpuBlitInitialize();
    HalIgpuPresentPrepare();
    return Found;
}

int VideoInitialize(void) {
    const BOOT_HANDOFF *Info = BootHandoffGet();
    VIDEO_CONFIG Video = BootHandoffToVideoConfig(Info);

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
