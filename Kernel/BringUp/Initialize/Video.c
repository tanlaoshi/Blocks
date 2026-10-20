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
     * 后缓冲一好就出 Logo，盖住后面核显探针、USB、挂盘、读字体的黑屏。
     * 进桌面前再 Finish。
     */
    DesktopBootLogoShow();
    (void)DisplayHardwareInitialize();
    return 0;
}
