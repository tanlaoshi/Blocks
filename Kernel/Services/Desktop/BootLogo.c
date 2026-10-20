/*
 * BootLogo.c — 内核只画 Logo 格子。
 * 屏上底色：Boot GOP 已铺；后缓冲：VideoInitialize 已铺且 DiscardDirty（勿再 Clear）。
 * 须在 igpu 探针之后调用，否则翻页/探针会跳帧。
 */
#include "Desktop.h"
#include "UI.h"
#include "Hal.h"
#include "HalVideo.h"
#include "HalDevices.h"
#include "SerialLog.h"
#include "BootTypes.h"

#define LOGO_BG     0x00000000u
#define LOGO_ACCENT 0x00C07820u
#define LOGO_TEAL   0x001E9A8Eu
#define LOGO_CELL   48u
#define LOGO_GAP    8u
#define LOGO_OPEN   112u
/* 清屏后略停再画 Logo */
#define LOGO_SHOW_DELAY_MS 280u
/* 每 1% 帧最短间隔：早期模块 Set 连跳时匀速下落（约 100×22ms ≈ 2.2s 满程） */
#define LOGO_FRAME_MS 22u
/* 至少约 1 秒（按 ~3GHz 估），读盘已久则 Finish 几乎不再空等 */
#define LOGO_MIN_MS 1000u
#define LOGO_TSC_PER_MS 3000000ull
#define LOGO_SHIFT_NONE 0xFFFFFFFFu

static int gLogoUp;
static int gTealBottomUp; /* 底部三格青绿只画一次 */
static UINT64 gLogoTsc;
static UINT32 gPct;
static UINT32 gAx;
static UINT32 gAy;
static UINT32 gMarkW;
static UINT32 gMarkH;
static UINT32 gLastShift; /* 上一帧橙色 Shift；用于只擦脚印 */

/* 与 Drag/Slide 相同：抬高条带，整块 Present，避免 64 行撕开 */
static void LogoPresentFlush(void) {
    HalVideoSetPresentChunkRows(16384u);
    HalVideoPresentFlush();
    HalVideoSetPresentChunkRows(0);
}

static UINT64 LogoTsc(void) {
#if defined(__x86_64__)
    UINT32 Lo;
    UINT32 Hi;

    __asm__ volatile ("rdtsc" : "=a"(Lo), "=d"(Hi));
    return ((UINT64)Hi << 32) | Lo;
#else
    static UINT64 Fake;

    Fake += LOGO_TSC_PER_MS;
    return Fake;
#endif
}

static void LogoSpinMs(UINT32 Ms) {
    UINT64 Need = (UINT64)Ms * LOGO_TSC_PER_MS;
    UINT64 Start = LogoTsc();

    while (LogoTsc() - Start < Need) {
        HalInputPoll();
        HalCpuRelax();
    }
}

static void Fill(UINT32 X, UINT32 Y, UINT32 W, UINT32 H, UINT32 C) {
    if (W == 0 || H == 0) {
        return;
    }
    GraphicsFillRectangle(X, Y, W, H, C);
}

static void Cell(UINT32 X, UINT32 Y, UINT32 Col, UINT32 Row, UINT32 C) {
    UINT32 Step = LOGO_CELL + LOGO_GAP;

    Fill(X + Col * Step, Y + Row * Step, LOGO_CELL, LOGO_CELL, C);
}

/* Shift 是 T 还悬在凹形上方的距离。0 时竖条落进凹口。 */
static void DrawOrange(UINT32 X, UINT32 Y, UINT32 Shift) {
    UINT32 Yt = (Y > Shift) ? (Y - Shift) : 0;

    Cell(X, Yt, 0, 0, LOGO_ACCENT);
    Cell(X, Yt, 1, 0, LOGO_ACCENT);
    Cell(X, Yt, 2, 0, LOGO_ACCENT);
    Cell(X, Yt, 1, 1, LOGO_ACCENT);
}

/* 只擦上一帧橙色四格，绝不碰青绿（整带擦会闪线）。 */
static void ClearOrange(UINT32 X, UINT32 Y, UINT32 Shift) {
    UINT32 Yt = (Y > Shift) ? (Y - Shift) : 0;
    UINT32 Step = LOGO_CELL + LOGO_GAP;

    Fill(X + 0 * Step, Yt + 0 * Step, LOGO_CELL, LOGO_CELL, LOGO_BG);
    Fill(X + 1 * Step, Yt + 0 * Step, LOGO_CELL, LOGO_CELL, LOGO_BG);
    Fill(X + 2 * Step, Yt + 0 * Step, LOGO_CELL, LOGO_CELL, LOGO_BG);
    Fill(X + 1 * Step, Yt + 1 * Step, LOGO_CELL, LOGO_CELL, LOGO_BG);
}

static void DrawTealSides(UINT32 X, UINT32 Y) {
    Cell(X, Y, 0, 1, LOGO_TEAL);
    Cell(X, Y, 2, 1, LOGO_TEAL);
}

static void DrawTealBottom(UINT32 X, UINT32 Y) {
    Cell(X, Y, 0, 2, LOGO_TEAL);
    Cell(X, Y, 1, 2, LOGO_TEAL);
    Cell(X, Y, 2, 2, LOGO_TEAL);
}

static void PaintPieces(UINT32 Pct) {
    UINT32 Shift;

    if (Pct > 100) {
        Pct = 100;
    }
    Shift = (100 - Pct) * LOGO_OPEN / 100;
    if (gLastShift != LOGO_SHIFT_NONE) {
        ClearOrange(gAx, gAy, gLastShift);
    }
    DrawOrange(gAx, gAy, Shift);
    /* 两侧每帧点一下（两格很轻），竖条贴近凹口时也不会被擦出闪线 */
    DrawTealSides(gAx, gAy);
    if (!gTealBottomUp) {
        DrawTealBottom(gAx, gAy);
        gTealBottomUp = 1;
    }
    gLastShift = Shift;
    gPct = Pct;
}

static void LogoHoldUntilMin(void) {
    UINT64 Need = (UINT64)LOGO_MIN_MS * LOGO_TSC_PER_MS;
    UINT64 Start = gLogoTsc;

    if (Start == 0) {
        Start = LogoTsc();
    }
    while (LogoTsc() - Start < Need) {
        HalInputPoll();
        HalCpuRelax();
    }
}

void DesktopBootLogoBump(UINT32 Cap) {
    UINT32 Next;

    if (!gLogoUp || Cap <= gPct) {
        return;
    }
    Next = gPct + 1;
    if (Next > Cap) {
        Next = Cap;
    }
    if (Next > 100) {
        Next = 100;
    }
    PaintPieces(Next);
    LogoPresentFlush();
    LogoSpinMs(LOGO_FRAME_MS);
}

void DesktopBootLogoShow(void) {
    UINT32 W;
    UINT32 H;
    UINT32 MarkW;
    UINT32 MarkH;

    if (gLogoUp) {
        return;
    }
    HalVideoGetSize(&W, &H);
    if (W < 320 || H < 200) {
        return;
    }

    LogoSpinMs(LOGO_SHOW_DELAY_MS);

    MarkW = LOGO_CELL * 3 + LOGO_GAP * 2;
    MarkH = LOGO_CELL * 3 + LOGO_GAP * 2;
    gMarkW = MarkW;
    gMarkH = MarkH;
    gAx = (W > MarkW) ? (W - MarkW) / 2 : 0;
    gAy = (H > MarkH) ? (H - MarkH) / 2 : 0;
    /* 底色不碰 LFB；后缓冲已在 VideoInitialize 铺好。只 Present 格子。 */
    gPct = 0;
    gTealBottomUp = 0;
    gLastShift = LOGO_SHIFT_NONE;
    PaintPieces(0);
    LogoPresentFlush();
    LogBoot("Boot: Logo\n");
    gLogoTsc = LogoTsc();
    gLogoUp = 1;
    HalBootLogoBumpRegister(DesktopBootLogoBump);
}

void DesktopBootLogoSet(UINT32 Pct) {
    UINT32 Cur;

    if (!gLogoUp || Pct <= gPct) {
        return;
    }
    if (Pct > 100) {
        Pct = 100;
    }
    /* 大阶段拆成 1% 步进；每帧限速，避免早期模块连跳时下落过猛 */
    Cur = gPct;
    while (Cur < Pct) {
        Cur++;
        PaintPieces(Cur);
        LogoPresentFlush();
        LogoSpinMs(LOGO_FRAME_MS);
        HalInputPoll();
    }
}

void DesktopBootLogoFinish(void) {
    if (!gLogoUp) {
        DesktopBootLogoShow();
    }
    DesktopBootLogoSet(100);
    /* 合成后的九格再停大约一秒；勿清黑——GuiDesktopInitialize→Redraw 会直接盖住。 */
    gLogoTsc = LogoTsc();
    LogoHoldUntilMin();
    LogBoot("Boot: Logo Done\n");
    HalBootLogoBumpRegister(0);
    gLogoUp = 0;
    gTealBottomUp = 0;
    gLastShift = LOGO_SHIFT_NONE;
    gLogoTsc = 0;
    gPct = 0;
}
