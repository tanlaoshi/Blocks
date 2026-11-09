/*
 * HAL/Common/HalVideo.c — 非 x86 HalVideo 门面：模式 / Present / 后缓冲 / 缩放
 *
 * 服务：QEMU virt ramfb、Pi mailbox FB。不属 Virtio 设备层。
 * FB Device/dcache → HalVideoFb.c；像素/文字 → HalVideoDraw.c（对齐 X64 三分法）。
 */
#include "HalVideo.h"
#include "Video/Video.h"
#include "PhysicalMemory.h"
#include "BoardConfig.h"

#ifndef BOARD_FB_SWAP_RB
#define BOARD_FB_SWAP_RB 0
#endif

/* VideoFrameBuffer* 在 Video.h */

void HalVideoSet(const VIDEO_CONFIG *Config) {
    VIDEO_CONFIG Local;

    if (!Config) {
        Local.FrameBufferBase = 0;
        Local.FrameBufferSize = 0;
        Local.HorizontalResolution = 0;
        Local.VerticalResolution = 0;
        Local.PixelsPerScanLine = 0;
    } else {
        Local = *Config;
    }
    VideoSet(&Local);
#if BOARD_FB_SWAP_RB
    VideoSetFbSwapRb(1);
#endif
}

void HalVideoSetFbSwapRb(int On) {
    VideoSetFbSwapRb(On);
}

void HalVideoInitializeBackbuffer(void) {
    UINT32 W;
    UINT32 H;
    UINT64 Bytes;
    UINT32 Pages;
    UINT32 *Buf;

    VideoReleaseBackbuffer();
    /* PR-23E：后缓冲按绘制尺寸分配 —— 任何 UI 比例都是物理分辨率 */
    VideoGetDrawSize(&W, &H);
    if (W == 0 || H == 0) {
        return;
    }
    Bytes = (UINT64)W * (UINT64)H * sizeof(UINT32);
    Pages = (UINT32)((Bytes + PAGE_SIZE - 1) / PAGE_SIZE);
    if (Pages == 0) {
        return;
    }
    Buf = (UINT32 *)PhysicalMemoryAllocatePages(Pages);
    if (Buf == 0) {
        return;
    }
    VideoSetBackbuffer(Buf, Pages);
}

int HalVideoCanHotSetMode(void) {
    return 0;
}

int HalVideoSetMode(UINT32 Width, UINT32 Height) {
    (void)Width;
    (void)Height;
    return -1;
}

UINT32 HalVideoModeCount(void) {
    return 0;
}

int HalVideoModeGet(UINT32 Index, UINT32 *Width, UINT32 *Height) {
    (void)Index;
    (void)Width;
    (void)Height;
    return -1;
}

UINT64 HalVideoFrameBufferBase(void) {
    return VideoFrameBufferBase();
}

UINT64 HalVideoFrameBufferSize(void) {
    return VideoFrameBufferSize();
}

void HalVideoSetScanout(UINT32 *Va, UINT32 PitchPx, UINT64 Phys, UINT64 Size) {
    (void)Va;
    (void)PitchPx;
    (void)Phys;
    (void)Size;
}

void HalVideoPresent(void) {
    /*
     * 增量 Present 不整屏 dcache sync：Shell 环每圈 Present，全 FB clean
     * 会把 mini-UART TX 饿到「串口没日志、但还能收 halt」。
     * 整屏可见靠 PresentFlush / EndFront。
     */
    VideoPresent();
}

void HalVideoPresentCursor(void) {
    VideoPresentCursor();
    /*
     * Pi VC≠CPU cache：光标只走 PresentCursor 时若不清 dcache，
     * 桌面（PresentFlush 已 sync）看得见，箭头困在 D-cache → 屏上无光标。
     * 仅刷光标路径；Shell 环普通 Present 仍不 sync（护 mini-UART）。
     */
    HalVideoFbDcacheSync();
}

/* QEMU Virtio/GOP：无硬件光标平面 → 恒回退软件末波 */
int HalVideoHwCursorSupported(void) {
    return 0;
}

int HalVideoHwCursorEnable(const UINT32 *Argb, UINT32 Width, UINT32 Height,
                           UINT32 HotX, UINT32 HotY) {
    (void)Argb;
    (void)Width;
    (void)Height;
    (void)HotX;
    (void)HotY;
    return 0;
}

void HalVideoHwCursorSetPos(UINT32 X, UINT32 Y) {
    (void)X;
    (void)Y;
}

void HalVideoHwCursorDisable(void) {
}

/*
 * PresentFlush：条带 Present 期间推迟 sync，整屏拷完再一次 clean
 *（见 HalVideoFb.c 文件头注释）。
 */
static int gFbSyncDefer;

void HalVideoPresentFlush(void) {
    gFbSyncDefer = 1;
    VideoPresentFlush();
    gFbSyncDefer = 0;
    HalVideoFbDcacheSync();
}

void HalVideoDiscardDirty(void) {
    VideoDiscardDirty();
}

void HalVideoMarkDirty(UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    DirtyUnion(X, Y, W, H);
}

/* 与 X64 同：Logo/拖窗可抬高条带，避免 64 行撕成横线残影 */
void HalVideoSetPresentChunkRows(UINT32 Rows) {
    VideoSetPresentChunkRows(Rows);
}

void HalVideoSetGlyphSmooth(int On) {
    VideoSetGlyphSmooth(On);
}

void HalVideoDrawBeginFront(void) {
    VideoDrawBeginFront();
}

void HalVideoDrawEndFront(void) {
    VideoDrawEndFront();
    /* 真机 FB：与 Present 同路径 HalVideoFbDcacheSync（virt 同调无害） */
    HalVideoFbDcacheSync();
}

void HalVideoDrawEndFrontNoSync(void) {
    VideoDrawEndFront();
}

int HalVideoBackbufferEnabled(void) {
    return VideoBackbufferEnabled();
}

UINT64 HalVideoBackbufferBase(void) {
    return VideoBackbufferBase();
}

void HalVideoGetSize(UINT32 *Width, UINT32 *Height) {
    VideoGetSize(Width, Height);
}

UINT32 HalVideoGetUiScale(void) {
    return VideoGetUiScale();
}

UINT32 HalVideoGetDrawNum(void) {
    return VideoGetDrawNum();
}

UINT32 HalVideoGetDrawDen(void) {
    return VideoGetDrawDen();
}

UINT32 HalVideoGetDrawScale(void) {
    return VideoGetDrawScale();
}

void HalVideoGetPhysicalSize(UINT32 *Width, UINT32 *Height) {
    VideoGetPhysicalSize(Width, Height);
}

int HalVideoSetUiScale(UINT32 Percent) {
    if (VideoSetUiScale(Percent) != 0) {
        return -1;
    }
    HalVideoInitializeBackbuffer();
    if (!VideoBackbufferEnabled() && Percent != 100 &&
        VideoGetUiScale() != 100) {
        (void)VideoSetUiScale(100);
        HalVideoInitializeBackbuffer();
        return -1;
    }
    return 0;
}
