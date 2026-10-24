/*
 * HAL/Virtio/HalVideo.c — 帧缓冲门面，委托 Drivers/Video（PR-V2 ramfb scanout）
 */
#include "HalVideo.h"
#include "Video/Video.h"
#include "PhysicalMemory.h"
#include "Hal.h"

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
}

void HalVideoInitializeBackbuffer(void) {
    UINT32 W;
    UINT32 H;
    UINT64 Bytes;
    UINT32 Pages;
    UINT32 *Buf;

    VideoReleaseBackbuffer();
    VideoGetSize(&W, &H);
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

/* PR-G-fb-pte：x86 才有 PWT/PCD；virt 空实现 */
void HalVideoLogFbPte(void) {
}

int HalVideoFbPteLine(char *Buf, UINTN Max) {
    if (Buf && Max > 0) {
        Buf[0] = 0;
    }
    return 0;
}

void HalVideoEnableFbWc(void) {
}

UINT64 HalVideoFbMapFlags(void) {
    return HAL_PAGE_PRESENT | HAL_PAGE_WRITABLE;
}

/*
 * Pi VC 与 CPU 不同 cache 域：凡写 scanout（Present / front 直写）后须 clean。
 * 只 EndFront 刷、Present 不刷 → GuiRedraw 桌面困在 D-cache，屏上仍是 boot 黑字。
 *
 * PresentFlush 会连打多次 Present（默认 64 行条带）。若每条都 sync，HDMI 上会
 * 「一条条撕开」：上半深色桌面贴 U-Boot 黑底不易察觉，浅色任务栏一到就像
 * 「U-Boot 界面底部先出半个桌面」。Flush 期间推迟 sync，整屏拷完再一次 clean。
 */
static int gFbSyncDefer;

static void FbDcacheSync(void) {
    UINT64 Base = VideoFrameBufferBase();
    UINT64 Size = VideoFrameBufferSize();

    if (Base != 0 && Size != 0) {
        HalDcacheCleanInvalidateRange((void *)(UINTN)Base, (UINTN)Size);
    }
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

void HalVideoPresentFlush(void) {
    gFbSyncDefer = 1;
    VideoPresentFlush();
    gFbSyncDefer = 0;
    FbDcacheSync();
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
    /* 真机 FB：与 Present 同路径 FbDcacheSync（virt 同调无害） */
    FbDcacheSync();
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

void HalVideoDrawPixel(UINT32 X, UINT32 Y, UINT32 Color) {
    VideoDrawPixel(X, Y, Color);
}

void HalVideoDrawPixelRaw(UINT32 X, UINT32 Y, UINT32 Color) {
    VideoDrawPixelRaw(X, Y, Color);
}

void HalVideoXorPixelRaw(UINT32 X, UINT32 Y, UINT32 Mask) {
    VideoXorPixelRaw(X, Y, Mask);
}

void HalVideoCursorOverlayBegin(void) {
    VideoCursorOverlayBegin();
}

void HalVideoCursorOverlayEnd(void) {
    VideoCursorOverlayEnd();
}

UINT32 HalVideoReadPixel(UINT32 X, UINT32 Y) {
    return VideoReadPixel(X, Y);
}

void HalVideoFillRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color) {
    VideoFillRect(X, Y, Width, Height, Color);
}

UINT32 HalVideoBlendRgb(UINT32 Dst, UINT32 Src, UINT8 Alpha) {
    return VideoBlendRgb(Dst, Src, Alpha);
}

void HalVideoBlendPixel(UINT32 X, UINT32 Y, UINT32 Color, UINT8 Alpha) {
    VideoBlendPixel(X, Y, Color, Alpha);
}

void HalVideoBlendPixelRaw(UINT32 X, UINT32 Y, UINT32 Color, UINT8 Alpha) {
    VideoBlendPixelRaw(X, Y, Color, Alpha);
}

void HalVideoBlendFillRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height,
                           UINT32 Color, UINT8 Alpha) {
    VideoBlendFillRect(X, Y, Width, Height, Color, Alpha);
}

void HalVideoCopyRect(UINT32 SrcX, UINT32 SrcY, UINT32 DstX, UINT32 DstY,
                      UINT32 Width, UINT32 Height) {
    VideoCopyRect(SrcX, SrcY, DstX, DstY, Width, Height);
}

void HalVideoReadRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 *Out) {
    VideoReadRect(X, Y, Width, Height, Out);
}

void HalVideoWriteRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, const UINT32 *In) {
    VideoWriteRect(X, Y, Width, Height, In);
}

void HalVideoClearScreen(UINT32 Color) {
    VideoClearScreen(Color);
}

void HalVideoDrawCharAt(UINT32 X, UINT32 Y, char C, UINT32 Color) {
    VideoDrawCharAt(X, Y, C, Color);
}

void HalVideoDrawCodepointAt(UINT32 X, UINT32 Y, UINT32 Cp, UINT32 Color) {
    VideoDrawCodepointAt(X, Y, Cp, Color);
}

void HalVideoDrawStringAt(UINT32 X, UINT32 Y, const char *Text, UINT32 Color) {
    VideoDrawStringAt(X, Y, Text, Color);
}

void HalVideoDrawChar(char C, UINT32 Color) {
    VideoDrawChar(C, Color);
}

void HalVideoDrawString(const char *Text, UINT32 Color) {
    VideoDrawString(Text, Color);
}

void HalVideoEraseLastChar(void) {
    VideoEraseLastChar();
}

void HalVideoSetClipRegion(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Background) {
    VideoSetClipRegion(X, Y, Width, Height, Background);
}

void HalVideoSetClipOrigin(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Background) {
    VideoSetClipOrigin(X, Y, Width, Height, Background);
}

void HalVideoGetTextCursor(UINT32 *X, UINT32 *Y) {
    VideoGetTextCursor(X, Y);
}

void HalVideoSetTextCursor(UINT32 X, UINT32 Y) {
    VideoSetTextCursor(X, Y);
}

void HalVideoClearClip(void) {
    VideoClearClip();
}

void HalVideoScrollClipLines(int Delta) {
    VideoScrollClipLines(Delta);
}
