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
#include "Font.h"
#include "ToySerialLog.h"
#include "BootTypes.h"

#define LOGO_BG     0x00101828u
#define LOGO_FG     0x00E8EEF4u
#define LOGO_ACCENT 0x00F0A030u
#define LOGO_TEAL   0x002EC4B6u
#define LOGO_TEXT_SCALE 3u
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
    UiFillRectangle(X, Y, W, H, C);
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

/* 内建字是 10×18，标题按 3 倍点阵画，大约 54 像素高。 */
static UINT32 TitleWidth(const char *S) {
    const FONT_FACE *F = FontGetCurrent();
    UINT32 N = 0;

    if (F == 0) {
        return FontStringWidth(S);
    }
    while (S[N] != 0) {
        N++;
    }
    if (N == 0) {
        return 0;
    }
    return N * (F->Width + F->CharSpacing) * LOGO_TEXT_SCALE - F->CharSpacing * LOGO_TEXT_SCALE;
}

static void DrawTitle(UINT32 X, UINT32 Y, const char *S, UINT32 Color) {
    const FONT_FACE *F = FontGetCurrent();
    UINT32 Step;
    UINT32 i;

    if (F == 0 || F->BytesPerRow == 0) {
        HalVideoDrawStringAt(X, Y, S, Color);
        return;
    }
    Step = (F->Width + F->CharSpacing) * LOGO_TEXT_SCALE;
    for (i = 0; S[i] != 0; i++) {
        const UINT8 *Glyph = FontGlyph(S[i]);
        UINT32 Row;
        UINT32 Col;

        if (Glyph == 0) {
            continue;
        }
        for (Row = 0; Row < F->Height; Row++) {
            for (Col = 0; Col < F->Width; Col++) {
                UINT8 Byte = Glyph[Row * F->BytesPerRow + (Col / 8u)];
                UINT32 Bit = 7u - (Col % 8u);

                if ((Byte & (1u << Bit)) == 0) {
                    continue;
                }
                Fill(X + i * Step + Col * LOGO_TEXT_SCALE,
                     Y + Row * LOGO_TEXT_SCALE,
                     LOGO_TEXT_SCALE, LOGO_TEXT_SCALE, Color);
            }
        }
    }
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
    UINT32 TextW;
    UINT32 TextX;
    const char *Title = "Blocks";

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
    {
        UINT32 Span = MarkH + LOGO_OPEN + 20 + 54;

        gAy = (H > Span) ? ((H - Span) / 2 + LOGO_OPEN) : LOGO_OPEN;
    }
    TextW = TitleWidth(Title);
    TextX = (W > TextW) ? (W - TextW) / 2 : 0;
    DrawTitle(TextX, gAy + MarkH + 20, Title, LOGO_FG);
    /* 引导读盘时 T 已落到 40，这里接上，避免弹回顶部。 */
    PaintPieces(40);
    HalVideoPresentFlush();
    ToyLogBoot("Boot: Logo\n");
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
    ToyLogBoot("Boot: Logo Cleared\n");
    gLogoUp = 0;
    gLogoTsc = 0;
    gPct = 0;
}
