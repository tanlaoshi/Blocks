/*
 * HalVideo.c — PR-S3-halvideo-1：模式 / Present / 后缓冲 / 缩放
 *
 * FB WC 见 HalVideoFb.c；像素/文字绘制见 HalVideoDraw.c。
 */
#include "HalVideo.h"
#include "Video/Video.h"
#include "Hotres.h"
#include "PhysicalMemory.h"
#include "HalDevices.h"
#include "BootHandoff.h"

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

void HalVideoSetFbSwapRb(int On) {
    VideoSetFbSwapRb(On);
}

/*
 * PR-G9：PMM 分配后缓冲并挂上。须在 PhysicalMemoryInitialize 之后调用。
 * PR-23E：尺寸按绘制尺寸（恒为物理分辨率），不再用逻辑宽高。
 * 分配失败则保持直写 GOP（功能仍可用，仍可能撕裂）。
 */
void HalVideoInitializeBackbuffer(void) {
    UINT32 W;
    UINT32 H;
    UINT64 Bytes;
    UINT32 Pages;
    UINT32 *Buf;

    /*
     * PR-fix-ui-scale-flash：UI 缩放重建时物理尺寸不变，翻页页仍有效。
     * 走 Rebuild 保留当前 scanout 页继续扫描；旧代码的 Invalidate 会先
     * 把 SURF 切回 LFB，若 LFB 里冻着开机 logo，新帧没画好就整屏闪一帧。
     */
    HalIgpuPresentRebuild();
    VideoReleaseBackbuffer();
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
    HalIgpuPresentPrepare();
}

int HalVideoCanHotSetMode(void) {
    /*
     * 只有 Bochs VBE（QEMU 标准 VGA）能真正热切分辨率。
     * 真机走 Boot 交接的 GOP：EBS 之后 live SetMode 会炸（NUC：#PF → #UD@0），
     * VideoGopSetMode 里已经是「拒绝」逻辑——那这里就不能报「能热切」，
     * 否则只有「当前分辨率」这一项会因为「已经在目标模式」被误判成即时生效，
     * 出现「1920x1080 点了立刻刷新、其它都要重启」的不一致。
     */
    return VideoBochsAvailable();
}

int HalVideoSetMode(UINT32 Width, UINT32 Height) {
    int Rc;

    if (VideoBochsAvailable()) {
        Rc = VideoBochsSetMode(Width, Height);
    } else {
        Rc = VideoGopSetMode(Width, Height);
    }
    if (Rc != 0) {
        return -1;
    }
    HalVideoInitializeBackbuffer();
    return 0;
}

UINT32 HalVideoModeCount(void) {
    const BOOT_HANDOFF *Info = BootHandoffGet();

    if (!Info || Info->VideoModeCount == 0) {
        return 0;
    }
    if (Info->VideoModeCount > BOOT_VIDEO_MODE_MAX) {
        return BOOT_VIDEO_MODE_MAX;
    }
    return Info->VideoModeCount;
}

int HalVideoModeGet(UINT32 Index, UINT32 *Width, UINT32 *Height) {
    const BOOT_HANDOFF *Info = BootHandoffGet();

    if (!Info || Index >= Info->VideoModeCount || Index >= BOOT_VIDEO_MODE_MAX) {
        return -1;
    }
    if (Width) {
        *Width = Info->VideoModes[Index].Width;
    }
    if (Height) {
        *Height = Info->VideoModes[Index].Height;
    }
    return 0;
}

UINT64 HalVideoFrameBufferBase(void) {
    return VideoFrameBufferBase();
}

UINT64 HalVideoFrameBufferSize(void) {
    return VideoFrameBufferSize();
}

void HalVideoSetScanout(UINT32 *Va, UINT32 PitchPx, UINT64 Phys, UINT64 Size) {
    VideoSetScanout(Va, PitchPx, Phys, Size);
}

void HalVideoPresent(void) {
    VideoPresent();
}

void HalVideoPresentCursor(void) {
    VideoPresentCursor();
}

void HalVideoPresentFlush(void) {
    VideoPresentFlush();
}

void HalVideoDiscardDirty(void) {
    VideoDiscardDirty();
}

void HalVideoMarkDirty(UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    DirtyUnion(X, Y, W, H);
}

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
}

void HalVideoDrawEndFrontNoSync(void) {
    VideoDrawEndFront();
}

void HalVideoFbDcacheSync(void) {
    /* X64 GOP 无 VC 别名；sync 无操作 */
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
        /* 无后缓冲无法缩放 Present；退回 100% */
        (void)VideoSetUiScale(100);
        HalVideoInitializeBackbuffer();
        return -1;
    }
    return 0;
}
