/*
 * IgpuPresentPrep.c — 启动期映后缓冲 + 屏外 SRC_COPY 探针（igpu-4 / igpu-6）
 *
 * igpu-6：scratch 页 XY_SRC_COPY，CPU 读回成功才置 PresentCopyOk。
 * igpu-7：CopyOk 后调 IgpuScanoutInit；失败仍脏矩形 SRC_COPY（不改 CopyOk）。
 */
#include "Igpu.h"
#include "Hal.h"
#include "HalVideo.h"
#include "BootHandoff.h"
#include "PhysicalMemory.h"
#include "VirtualMemory.h"
#include "SerialLog.h"
#include "IgpuBlitPrivate.h"

#ifndef PTE_PWT
#define PTE_PWT HAL_PAGE_PWT
#define PTE_PCD HAL_PAGE_PCD
#endif

#define IGPU_PROBE_GTT_SRC  0x05800000ull /* 勿复用 SCRATCH：mem 自检品红仍占 0x02000000 */
#define IGPU_PROBE_GTT_DST  0x05801000ull
#define IGPU_PROBE_W        64u
#define IGPU_PROBE_H        16u
#define IGPU_PROBE_PITCH_B  IGPU_MEM_PITCH
#define IGPU_PROBE_COLOR    0x00C0FFEEu

static int gPresentLogged;
static int gPresentCopyOk;
static int gRebuildLogged;

int IgpuPresentCopyOk(void) {
    return gPresentCopyOk;
}

UINT64 IgpuFrontGttBase(void) {
    return (UINT64)IgpuGttSurf();
}

void IgpuPresentInvalidate(void) {
    gPresentLogged = 0;
    gPresentCopyOk = 0;
    IgpuScanoutInvalidate();
    IgpuBackInvalidate();
}

/* 小开销 LFB 抽样 hash（8×8 步长，供真机确认冻结帧；一次性日志用） */
static UINT32 LfbSampleHash(void) {
    const UINT32 *P;
    UINT64 Base;
    UINT32 W = 0;
    UINT32 H = 0;
    UINT32 Pitch;
    UINT32 X;
    UINT32 Y;
    UINT32 Hsh = 2166136261u;
    const BOOT_HANDOFF *Info;

    Base = HalVideoFrameBufferBase();
    HalVideoGetPhysicalSize(&W, &H);
    Info = BootHandoffGet();
    Pitch = Info ? Info->PixelsPerScanLine : W;
    if (Base == 0 || W == 0 || H == 0 || Pitch == 0) {
        return 0;
    }
    P = (const UINT32 *)(UINTN)Base;
    for (Y = 0; Y < H; Y += 8u) {
        for (X = 0; X < W; X += 8u) {
            Hsh = (Hsh ^ P[(UINT64)Y * Pitch + X]) * 16777619u;
        }
    }
    return Hsh;
}

/*
 * 后缓冲重建（UI 缩放）：物理尺寸没变，已翻上屏的 scanout 页继续扫描。
 *
 * 不能调 IgpuScanoutInvalidate：它会把 PLANE_SURF 立刻写回固件旧面（LFB），
 * 而新后缓冲此时还没分配/重绘——那一下就把 LFB 里冻结的开机 logo 整屏
 * 露出来（切档「闪黑底 + logo」的根因）。这里只丢掉后缓冲 GTT 映射，
 * 让随后的 IgpuPresentPrepare 重新映射新页；gScanLive/gScanOk/gShown
 * 与 gFront 原样保留，屏幕停在重绘前的最后一帧桌面上。
 *
 * 只有几何不一致（分辨率变化/销毁）才走完整 Invalidate。
 */
void IgpuPresentRebuild(void) {
    UINT32 W = 0;
    UINT32 H = 0;
    UINT32 PitchPx;
    const BOOT_HANDOFF *Info;

    if (!IgpuReady() || !IgpuGttOk()) {
        if (!gRebuildLogged && HalVideoGetUiScale() != 100u) {
            gRebuildLogged = 1;
            IgpuLogS("Boot: rebuild: live=0 (no igpu) -> invalidate\n");
        }
        IgpuPresentInvalidate();
        return;
    }
    HalVideoGetPhysicalSize(&W, &H);
    Info = BootHandoffGet();
    PitchPx = Info ? Info->PixelsPerScanLine : W;
    if (PitchPx == 0) {
        PitchPx = W;
    }
    if (IgpuScanoutGeometryMatches(W, H, PitchPx)) {
        if (!gRebuildLogged && HalVideoGetUiScale() != 100u) {
            gRebuildLogged = 1;
            IgpuLogS("Boot: rebuild: live=");
            IgpuLogX((UINT32)IgpuScanoutLive());
            IgpuLogS(" shown=");
            IgpuLogX((UINT32)IgpuScanoutShown());
            IgpuLogS(" surf=0x");
            IgpuLogX(IgpuGttSurf());
            IgpuLogS(" lfbhash=0x");
            IgpuLogX(LfbSampleHash());
            IgpuLogS("\n");
        }
        gPresentLogged = 0;
        gPresentCopyOk = 0;
        IgpuBackInvalidate();
        return;
    }
    IgpuPresentInvalidate();
}

int IgpuFrontMapEnsure(UINT64 PhysBase, UINTN Bytes) {
    (void)PhysBase;
    (void)Bytes;
    return IgpuGttOk() ? 1 : 0;
}

/* 屏外两页：独立 GTT；COLOR_BLT 写 Src 再 SRC_COPY→Dst。不碰 scanout / scratch。 */
static int ProbeMapPage(UINT64 GttOff, UINT64 Phys) {
    UINT64 Pte;

    if (!IgpuGsmMap(GttOff, Phys)) {
        return 0;
    }
    Pte = IgpuGsmPteRead(GttOff);
    if ((Pte & ~0xFFFull) != (Phys & ~0xFFFull) || (Pte & 1ull) == 0) {
        return 0;
    }
    return 1;
}

static int ProbeSrcCopyOffscreen(void) {
    void *SrcPage;
    void *DstPage;
    volatile UINT32 *Src;
    volatile UINT32 *Dst;
    UINT64 SrcPhys;
    UINT64 DstPhys;
    UINT64 FlagsUc;
    UINT32 i;
    UINT32 N;
    UINT32 Last;
    UINT32 Words[8];
    UINT32 Mode;

    SrcPage = PhysicalMemoryAllocatePage();
    DstPage = PhysicalMemoryAllocatePage();
    if (!SrcPage || !DstPage) {
        LogDrv("Boot: igpu copy soft (oom)\n");
        return 0;
    }
    SrcPhys = (UINT64)(UINTN)SrcPage;
    DstPhys = (UINT64)(UINTN)DstPage;
    FlagsUc = PTE_PRESENT | PTE_WRITABLE | PTE_PWT | PTE_PCD;
    if (VirtualMemoryMapRange(SrcPhys, SrcPhys, PAGE_SIZE, FlagsUc) != 0 ||
        VirtualMemoryMapRange(DstPhys, DstPhys, PAGE_SIZE, FlagsUc) != 0) {
        LogDrv("Boot: igpu copy soft (uc)\n");
        return 0;
    }
    Src = (volatile UINT32 *)(UINTN)SrcPhys;
    Dst = (volatile UINT32 *)(UINTN)DstPhys;
    N = PAGE_SIZE / 4u;
    for (i = 0; i < N; i++) {
        Src[i] = 0xA5A5A5A5u;
        Dst[i] = 0xA5A5A5A5u;
    }
    __asm__ volatile ("mfence" ::: "memory");
    FlushCpu((const void *)(UINTN)SrcPhys, PAGE_SIZE);
    FlushCpu((const void *)(UINTN)DstPhys, PAGE_SIZE);
    if (!ProbeMapPage(IGPU_PROBE_GTT_SRC, SrcPhys) ||
        !ProbeMapPage(IGPU_PROBE_GTT_DST, DstPhys)) {
        LogDrv("Boot: igpu copy soft (pte)\n");
        return 0;
    }

    Mode = IgpuMmioRead32(IGPU_RING_MODE);
    if ((Mode & IGPU_GRAPHICS_PPGTT_EN) != 0) {
        IgpuMmioWrite32(IGPU_RING_MODE, IGPU_FW_DISABLE(IGPU_GRAPHICS_PPGTT_EN));
    }
    if (!IgpuForcewakeGet()) {
        LogDrv("Boot: igpu copy soft (fw)\n");
        return 0;
    }

    /* 与 mem 自检相同：GPU COLOR_BLT 写源，避免 CPU 填的页 GPU 看不见 */
    Words[0] = IGPU_XY_COLOR_NOLEN | IGPU_BLT_WRITE_RGB | IGPU_BLT_WRITE_ALPHA | 5u;
    Words[1] = IGPU_ROP_COLOR_COPY | IGPU_BR13_DEPTH_32 | (IGPU_PROBE_PITCH_B & 0xFFFFu);
    Words[2] = 0;
    Words[3] = (IGPU_PROBE_H << 16) | IGPU_PROBE_W;
    Words[4] = (UINT32)IGPU_PROBE_GTT_SRC;
    Words[5] = 0;
    Words[6] = IGPU_PROBE_COLOR;
    Words[7] = IGPU_MI_NOOP;
    if (!EmitDwords(Words, 7)) {
        LogDrv("Boot: igpu copy soft (color)\n");
        return 0;
    }
    FlushCpu((const void *)(UINTN)SrcPhys, PAGE_SIZE);
    if ((Src[0] & 0x00FFFFFFu) != (IGPU_PROBE_COLOR & 0x00FFFFFFu)) {
        LogDrv("Boot: igpu copy soft (color pix=");
        LogDrvHex32(Src[0]);
        LogDrv(")\n");
        return 0;
    }

    if (!IgpuSrcCopyRect(0, 0, 0, 0, IGPU_PROBE_W, IGPU_PROBE_H,
                         IGPU_PROBE_PITCH_B, IGPU_PROBE_PITCH_B,
                         IGPU_PROBE_GTT_SRC, IGPU_PROBE_GTT_DST)) {
        LogDrv("Boot: igpu copy soft (emit)\n");
        return 0;
    }
    FlushCpu((const void *)(UINTN)DstPhys, PAGE_SIZE);
    Last = (IGPU_PROBE_H - 1u) * (IGPU_PROBE_PITCH_B / 4u);
    if ((Dst[0] & 0x00FFFFFFu) != (IGPU_PROBE_COLOR & 0x00FFFFFFu) ||
        (Dst[IGPU_PROBE_W - 1u] & 0x00FFFFFFu) != (IGPU_PROBE_COLOR & 0x00FFFFFFu) ||
        (Dst[Last] & 0x00FFFFFFu) != (IGPU_PROBE_COLOR & 0x00FFFFFFu)) {
        LogDrv("Boot: igpu copy soft (miss pix=");
        LogDrvHex32(Dst[0]);
        LogDrv(")\n");
        return 0;
    }
    return 1;
}

void IgpuPresentPrepare(void) {
    UINT64 Fb;
    UINT64 Back;
    UINT32 Bw;
    UINT32 Bh;
    UINTN BackBytes;

    if (!IgpuReady() || gPresentLogged) {
        return;
    }
    gPresentLogged = 1;
    gPresentCopyOk = 0;

    if (HalVideoGetUiScale() != 100u) {
        IgpuNotePresentSkipScale();
        return;
    }
    if (!IgpuGttOk()) {
        LogDrv("Boot: igpu present soft (no gtt)\n");
        return;
    }

    Fb = HalVideoFrameBufferBase();
    if (!HalVideoBackbufferEnabled()) {
        LogDrv("Boot: igpu present soft (no back)\n");
        return;
    }
    Back = HalVideoBackbufferBase();
    HalVideoGetSize(&Bw, &Bh);
    if (Back == 0 || Bw == 0 || Bh == 0) {
        LogDrv("Boot: igpu present soft (no back)\n");
        return;
    }
    BackBytes = (UINTN)Bw * (UINTN)Bh * 4u;
    if (!IgpuBackMap(Back, BackBytes)) {
        return;
    }

    LogDrv("Boot: igpu present ready fb=");
    LogDrvHex32((UINT32)Fb);
    LogDrv(" back=");
    LogDrvHex32((UINT32)Back);
    LogDrv("\n");

    if (!ProbeSrcCopyOffscreen()) {
        LogDrv("Boot: igpu present cpu fallback (probe miss)\n");
        return;
    }
    gPresentCopyOk = 1;
    LogDrv("Boot: igpu present copy ok\n");
    /* 翻页失败保持 CopyOk：Present 仍走脏矩形 SRC_COPY → gtt0 */
    (void)IgpuScanoutInit();
}
