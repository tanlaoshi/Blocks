/*
 * BootLogo.c — 启动 Logo → 清屏 → 再进 DesktopInitialize。
 * T 向下四格、凹口向上五格。一开始 T 悬在上面，读盘落到 40，
 * 进桌面前竖条填进凹口，合成三行、每行三块。凹形不动。格子尺寸两边相同。
 *
 * Video 后缓冲就绪即 Show，盖住核显 / USB / 挂盘 / 读字体。
 * Finish 时若屏上停得不够久，再补到大约一秒，避免一闪而过。
 */
#include "Desktop.h"
#include "UI.h"
#include "Hal.h"
#include "HalVideo.h"
#include "SerialLog.h"
#include "BootTypes.h"

#define LOGO_BG     0x00101828u
#define LOGO_ACCENT 0x00C07820u
#define LOGO_TEAL   0x001E9A8Eu
#define LOGO_CELL   48u
#define LOGO_GAP    8u
#define LOGO_OPEN   112u
/* 至少约 1 秒（按 ~3GHz 估），读盘已久则 Finish 几乎不再空等 */
#define LOGO_MIN_MS 1000u
#define LOGO_TSC_PER_MS 3000000ull

static int gLogoUp;
static UINT64 gLogoTsc;
static UINT32 gPct;
static UINT32 gAx;
static UINT32 gAy;
static UINT32 gMarkW;
static UINT32 gMarkH;

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

static void Fill(UINT32 X, UINT32 Y, UINT32 W, UINT32 H, UINT32 C) {
    if (W == 0 || H == 0) {
        return;
    }
    GfxFillRectangle(X, Y, W, H, C);
}

static void Cell(UINT32 X, UINT32 Y, UINT32 Col, UINT32 Row, UINT32 C) {
    UINT32 Step = LOGO_CELL + LOGO_GAP;

    Fill(X + Col * Step, Y + Row * Step, LOGO_CELL, LOGO_CELL, C);
}

/* Shift 是 T 还悬在凹形上方的距离。0 时竖条落进凹口。 */
static void DrawBlocks(UINT32 X, UINT32 Y, UINT32 Shift) {
    UINT32 Yt = (Y > Shift) ? (Y - Shift) : 0;
    UINT32 Yu = Y;

    Cell(X, Yt, 0, 0, LOGO_ACCENT);
    Cell(X, Yt, 1, 0, LOGO_ACCENT);
    Cell(X, Yt, 2, 0, LOGO_ACCENT);
    Cell(X, Yt, 1, 1, LOGO_ACCENT);
    /* 凹口在第二行正中，留给 T 的竖条。 */
    Cell(X, Yu, 0, 1, LOGO_TEAL);
    Cell(X, Yu, 2, 1, LOGO_TEAL);
    Cell(X, Yu, 0, 2, LOGO_TEAL);
    Cell(X, Yu, 1, 2, LOGO_TEAL);
    Cell(X, Yu, 2, 2, LOGO_TEAL);
}

static void PaintPieces(UINT32 Pct) {
    UINT32 Shift;
    UINT32 BandY;

    if (Pct > 100) {
        Pct = 100;
    }
    Shift = (100 - Pct) * LOGO_OPEN / 100;
    BandY = (gAy > LOGO_OPEN) ? (gAy - LOGO_OPEN) : 0;
    Fill(gAx, BandY, gMarkW, gMarkH + LOGO_OPEN, LOGO_BG);
    DrawBlocks(gAx, gAy, Shift);
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

    HalVideoClearScreen(LOGO_BG);

    MarkW = LOGO_CELL * 3 + LOGO_GAP * 2;
    MarkH = LOGO_CELL * 3 + LOGO_GAP * 2;
    gMarkW = MarkW;
    gMarkH = MarkH;
    gAx = (W > MarkW) ? (W - MarkW) / 2 : 0;
    /* 与引导相同：合成后的九格居中，T 从上方落下。 */
    gAy = (H > MarkH) ? (H - MarkH) / 2 : 0;
    /* 引导读盘时 T 已落到 40，这里接上，避免弹回顶部。 */
    PaintPieces(40);
    HalVideoPresentFlush();
    LogBoot("Boot: Logo\n");
    gLogoTsc = LogoTsc();
    gLogoUp = 1;
}

void DesktopBootLogoSet(UINT32 Pct) {
    if (!gLogoUp || Pct <= gPct) {
        return;
    }
    PaintPieces(Pct);
    HalVideoPresentFlush();
}

void DesktopBootLogoFinish(void) {
    if (!gLogoUp) {
        DesktopBootLogoShow();
    }
    DesktopBootLogoSet(100);
    /* 合成后的九格再停大约一秒，然后进桌面。 */
    gLogoTsc = LogoTsc();
    LogoHoldUntilMin();
    HalVideoClearScreen(0x00000000u);
    HalVideoPresentFlush();
    LogBoot("Boot: Logo Cleared\n");
    gLogoUp = 0;
    gLogoTsc = 0;
    gPct = 0;
}
