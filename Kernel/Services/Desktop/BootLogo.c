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
#include "SerialConfig.h"
#include "BootTypes.h"

#define LOGO_BG     0x00000000u
#define LOGO_ACCENT 0x008C5010u
#define LOGO_TEAL   0x00145850u
#define LOGO_CELL   48u
#define LOGO_GAP    8u
/* 橙 T 下落行程：仅一方块高；合体终位（gAy）不变 */
#define LOGO_OPEN   LOGO_CELL
/* 清屏后略停再画 Logo */
#define LOGO_SHOW_DELAY_MS 280u
/* 每 1% 帧最短间隔：早期模块 Set 连跳时匀速下落 */
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
/* gAx/gAy 是按哪一档逻辑画布算出来的（PR-19 修复/200% 残留） */
static UINT32 gLayoutW;
static UINT32 gLayoutH;

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

/*
 * 九宫格编号（左→右、上→下）：
 *   1 2 3
 *   4 5 6
 *   7 8 9
 * 橙 1/2/3/5 满高；青 4/6/8 上部减 1/3（底边对齐）；7/9 满高。
 * 面积：橙 4 格 = 青 4×(2/3)+2 = 4 格。
 */
static void Cell(UINT32 X, UINT32 Y, UINT32 Col, UINT32 Row, UINT32 C) {
    UINT32 Step = LOGO_CELL + LOGO_GAP;

    Fill(X + Col * Step, Y + Row * Step, LOGO_CELL, LOGO_CELL, C);
}

/* 底边对齐槽位，高度 ShortH（上部裁掉） */
static void CellBottom(UINT32 X, UINT32 Y, UINT32 Col, UINT32 Row,
                       UINT32 ShortH, UINT32 C) {
    UINT32 Step = LOGO_CELL + LOGO_GAP;
    UINT32 TopPad;

    if (ShortH == 0 || ShortH > LOGO_CELL) {
        return;
    }
    TopPad = LOGO_CELL - ShortH;
    Fill(X + Col * Step, Y + Row * Step + TopPad, LOGO_CELL, ShortH, C);
}

/* Shift 是 T 还悬在凹形上方的距离。0 时竖条落进凹口。 */
static void DrawOrange(UINT32 X, UINT32 Y, UINT32 Shift) {
    UINT32 Yt = (Y > Shift) ? (Y - Shift) : 0;

    Cell(X, Yt, 0, 0, LOGO_ACCENT); /* 1 */
    Cell(X, Yt, 1, 0, LOGO_ACCENT); /* 2 */
    Cell(X, Yt, 2, 0, LOGO_ACCENT); /* 3 */
    Cell(X, Yt, 1, 1, LOGO_ACCENT); /* 5 */
}

/*
 * 整段下落走廊抹黑再重画（QEMU 只擦旧∪新仍会拉烟横线）。
 * 止于橙 T 底，不盖底行青绿槽顶之上的 gap；底三格每帧重画。
 */
static void ClearOrangeCorridor(UINT32 X, UINT32 Y) {
    UINT32 Step = LOGO_CELL + LOGO_GAP;
    UINT32 Th = Step + LOGO_CELL;
    UINT32 Top = (Y > LOGO_OPEN) ? (Y - LOGO_OPEN) : 0;
    UINT32 Bot = Y + Th;
    UINT32 MarkW = LOGO_CELL * 3u + LOGO_GAP * 2u;

    if (Bot > Top) {
        Fill(X, Top, MarkW, Bot - Top, LOGO_BG);
        HalVideoMarkDirty(X, Top, MarkW, Bot - Top);
    }
}

static void DrawTealSides(UINT32 X, UINT32 Y) {
    UINT32 ShortH = (LOGO_CELL * 2u) / 3u;

    CellBottom(X, Y, 0, 1, ShortH, LOGO_TEAL); /* 4 上部减 1/3 */
    CellBottom(X, Y, 2, 1, ShortH, LOGO_TEAL); /* 6 */
}

static void DrawTealBottom(UINT32 X, UINT32 Y) {
    UINT32 ShortH = (LOGO_CELL * 2u) / 3u;

    Cell(X, Y, 0, 2, LOGO_TEAL);               /* 7 */
    CellBottom(X, Y, 1, 2, ShortH, LOGO_TEAL); /* 8 上部减 1/3 */
    Cell(X, Y, 2, 2, LOGO_TEAL);               /* 9 */
}

/*
 * PR-19 修复/200%：gAx/gAy 原来只在 Show 里按当时的逻辑尺寸算一次。
 * 真机 4K 默认 200%：VideoInitialize 时 gUiScale=100（逻辑=物理 3840×2160），
 * logo 画在正中；GuiInitialize 里 ThemeLoad 之后才 HalVideoSetUiScale(200)
 * （逻辑变 1920×1080，绘制×2）。旧 gAx/gAy=1840/1000 是上一档逻辑坐标，
 * 再按 ×2 落笔就被裁到右下角 → 右下角残留一块 logo（中间那份 100% 的
 * 旧后缓冲内容还在）。这里每帧核对画布：尺寸变了就按新逻辑尺寸重算，
 * 并把「旧 bbox（在新画布上正是残留处）+ 新 bbox」都抹黑，再让调用方
 * 按新坐标重画。坐标/尺寸全部保持逻辑值，×2 交给 GraphicsFillRectangle。
 */
static int LogoLayout(void) {
    UINT32 W;
    UINT32 H;
    UINT32 MarkW;
    UINT32 MarkH;
    UINT32 Ax;
    UINT32 Ay;

    HalVideoGetSize(&W, &H);
    if (W < 320 || H < 200) {
        return 0;
    }
    MarkW = LOGO_CELL * 3u + LOGO_GAP * 2u;
    MarkH = MarkW;
    Ax = (W > MarkW) ? (W - MarkW) / 2u : 0u;
    Ay = (H > MarkH) ? (H - MarkH) / 2u : 0u;
    if (gLayoutW == W && gLayoutH == H && gAx == Ax && gAy == Ay &&
        gMarkW == MarkW && gMarkH == MarkH) {
        return 1;
    }
    /* 先擦旧位（缩放切换后旧逻辑坐标就是屏上残留的物理位置） */
    if (gMarkW != 0 && gMarkH != 0) {
        Fill(gAx, gAy, gMarkW, gMarkH, LOGO_BG);
    }
    gAx = Ax;
    gAy = Ay;
    gMarkW = MarkW;
    gMarkH = MarkH;
    gLayoutW = W;
    gLayoutH = H;
    gLastShift = LOGO_SHIFT_NONE;
    /* 再擦新位：新后缓冲可能是复用的旧页，中间还留着上一档的 logo */
    Fill(gAx, gAy, gMarkW, gMarkH, LOGO_BG);
    return 1;
}

static void PaintPieces(UINT32 Pct) {
    UINT32 Shift;

    if (!LogoLayout()) {
        return;
    }
    if (Pct > 100) {
        Pct = 100;
    }
    Shift = (100 - Pct) * LOGO_OPEN / 100;
    ClearOrangeCorridor(gAx, gAy);
    DrawOrange(gAx, gAy, Shift);
    DrawTealSides(gAx, gAy);
    DrawTealBottom(gAx, gAy);
    gTealBottomUp = 1;
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

#if SCREEN_LOG
    /* 屏上 boot 日志与 logo 同 FB；再滑九格会像从日志页掉下来 */
    return;
#endif
    if (gLogoUp) {
        return;
    }
    HalVideoGetSize(&W, &H);
    if (W < 320 || H < 200) {
        return;
    }

    LogoSpinMs(LOGO_SHOW_DELAY_MS);

    /* 新一次开机：丢掉上一次的布局缓存，让 LogoLayout 按当前逻辑尺寸算 */
    gMarkW = 0;
    gMarkH = 0;
    gLayoutW = 0;
    gLayoutH = 0;
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
#if SCREEN_LOG
    return;
#endif
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
    gMarkW = 0;
    gMarkH = 0;
    gLayoutW = 0;
    gLayoutH = 0;
}
