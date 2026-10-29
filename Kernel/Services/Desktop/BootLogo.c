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
/* 九格整体外框（物理像素）：3 格 + 2 间隙 */
#define LOGO_MARK_W (LOGO_CELL * 3u + LOGO_GAP * 2u)
#define LOGO_MARK_H LOGO_MARK_W

static int gLogoUp;
static int gTealBottomUp; /* 底部三格青绿只画一次 */
static UINT64 gLogoTsc;
static UINT32 gPct;
static UINT32 gAx;
static UINT32 gAy;
static UINT32 gLastShift; /* 上一帧橙色 Shift；用于只擦脚印 */
/* gAx/gAy 是按哪一档物理分辨率算出来的（与 UI 缩放档无关） */
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

/*
 * 开机 Logo 永远按 100% 几何画：坐标/尺寸全是物理像素，直接按物理矩形
 * 落笔，不经过 GraphicsFillRectangle 的逻辑→物理换算，因此与
 * HalVideoGetDrawNum()/Den() 无关。缩放只作用于桌面，切档时 Logo 不重排。
 * 物理直写同时会把覆盖到的逻辑脏区记账，Present 再映射回物理，缩放档下
 * 也能把这几个格子刷上屏。
 */
static void Fill(UINT32 X, UINT32 Y, UINT32 W, UINT32 H, UINT32 C) {
    static UINT32 Row[LOGO_MARK_W];
    UINT32 i;
    UINT32 r;

    if (W == 0u || H == 0u) {
        return;
    }
    if (W > (UINT32)(sizeof(Row) / sizeof(Row[0]))) {
        W = (UINT32)(sizeof(Row) / sizeof(Row[0]));
    }
    for (i = 0; i < W; i++) {
        Row[i] = C;
    }
    for (r = 0; r < H; r++) {
        HalVideoWriteRectPhys(X, Y + r, W, 1u, Row);
    }
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
    UINT32 MarkW = LOGO_MARK_W;

    if (Bot > Top) {
        /* Fill 走物理直写，脏区由 HalVideoWriteRectPhys 记账 */
        Fill(X, Top, MarkW, Bot - Top, LOGO_BG);
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
 * 布局只按物理分辨率居中，与 HalVideoGetDrawNum()/Den() 无关。
 *
 * 开机 Logo 属于固件/引导阶段的品牌画面，不跟用户的桌面缩放设置走：
 * 物理尺寸不变（50/150/200% 都不改物理宽高）时这里只做查询，不擦不重排，
 * 于是 ThemeLoad 后 HalVideoSetUiScale() 切档不会再把 Logo 按新比例重画，
 * 也就没有「进桌面前 Logo 跳一下」。
 *
 * 若物理分辨率真的变了（正常开机流程不会），按新物理尺寸重新居中即可；
 * 坐标仍旧是 1:1，不做 Num/Den 换算。
 */
static int LogoLayout(void) {
    UINT32 W;
    UINT32 H;
    UINT32 Ax;
    UINT32 Ay;

    HalVideoGetPhysicalSize(&W, &H);
    if (W < 320u || H < 200u) {
        return 0;
    }
    Ax = (W > LOGO_MARK_W) ? (W - LOGO_MARK_W) / 2u : 0u;
    Ay = (H > LOGO_MARK_H) ? (H - LOGO_MARK_H) / 2u : 0u;
    if (gLayoutW == W && gLayoutH == H && gAx == Ax && gAy == Ay) {
        return 1;
    }
    gAx = Ax;
    gAy = Ay;
    gLayoutW = W;
    gLayoutH = H;
    gLastShift = LOGO_SHIFT_NONE;
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
    /*
     * 每帧整块抹黑自己的物理矩形：切档会重配后缓冲（可能是未初始化页），
     * 格间空隙必须是底色。这只是补背景，几何仍与缩放档无关；
     * 不按 Num/Den 重排，也不需要「先擦旧档再画新档」。
     */
    Fill(gAx, gAy, LOGO_MARK_W, LOGO_MARK_H, LOGO_BG);
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
    HalVideoGetPhysicalSize(&W, &H);
    if (W < 320u || H < 200u) {
        return;
    }

    LogoSpinMs(LOGO_SHOW_DELAY_MS);

    /* 新一次开机：丢掉上一次的布局缓存；几何只由物理分辨率决定 */
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
    gLayoutW = 0;
    gLayoutH = 0;
}
