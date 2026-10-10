/*
 * Layout.c — 窗/栏/图标几何描述表 + 按 FB 等比缩放（只缩不放）
 *
 * 【初学者】
 * - 分层：Core/Gui；LAYOUT.CFG 覆盖见 LayoutConfiguration.c
 * - 对外：LayoutSetFb / LayoutResolveWindow / LayoutIconSlot / LayoutStart*
 * - 勿在 WindowPaint 里写死像素；改表或 CFG
 */
#include "Layout.h"
#include "LayoutPrivate.h"
#include "Window.h"

static UINT32 gFbW;
static UINT32 gFbH;
UINT32 gBarH = 32u;   /* 吃下 Terminus/CJK 18 + 垫 */
static UINT32 gCloseW = 22u;
UINT32 gTitleH = 28u;
static UINT32 gIconX0 = 28u;
static UINT32 gIconY0 = 48u;
UINT32 gIconTile = 40u;
static UINT32 gIconGap = 6u;
static UINT32 gIconLabelH = 18u;
UINT32 gIconStride = 88u;
static UINT32 gStartButtonX = 6u;
static UINT32 gStartButtonWidth = 92u;
static UINT32 gStartButtonHeight = 22u;
static UINT32 gStartPadY = 4u;
static UINT32 gMenuW = 140u;
static UINT32 gMenuRow = 24u;
static UINT32 gMenuN = 4u;

/*
 * 内建描述（设计稿 1280×720）。
 * Place：CENTER=水平居中；XY=内容区左上；SHELL_DELTA=相对 Shell。
 */
GUI_WIN_LAYOUT gWin[GUI_WIN_COUNT] = {
    {"shell", 520u, 300u, 280u, 160u, GUI_PLACE_CENTER, 0, 24, 1},
    {"about", 340u, 200u, 200u, 120u, GUI_PLACE_SHELL_DELTA, 80, 60, 1},
    {"settings", 360u, 220u, 240u, 140u, GUI_PLACE_XY, 48, 48, 0},
    {"files", 380u, 260u, 240u, 160u, GUI_PLACE_XY, 72, 72, 0},
    {"store", 400u, 260u, 260u, 160u, GUI_PLACE_XY, 96, 56, 0},
};

/*
 * FitScalePermille — 设计稿→当前 FB 缩放千分比（上限 1000，下限 400）
 *
 * 谁调用：ScaleDesign（本文件内）。
 */
static UINT32 FitScalePermille(void) {
    UINT32 Sx;
    UINT32 Sy;
    UINT32 S;

    if (gFbW == 0 || gFbH == 0) {
        return 1000u;
    }
    Sx = (gFbW * 1000u) / GUI_LAYOUT_REF_W;
    Sy = (gFbH * 1000u) / GUI_LAYOUT_REF_H;
    S = (Sx < Sy) ? Sx : Sy;
    if (S > 1000u) {
        S = 1000u; /* 只缩不放 */
    }
    if (S < 400u) {
        S = 400u; /* 地板，避免控件不可点 */
    }
    return S;
}

/*
 * ScaleDesign — 设计像素乘当前缩放
 *
 * 谁调用：本文件各 Layout* 出口。
 */
static UINT32 ScaleDesign(UINT32 DesignPx) {
    return (UINT32)(((UINT64)DesignPx * (UINT64)FitScalePermille()) / 1000u);
}

/*
 * LayoutSetFb — 记录帧缓冲尺寸供缩放与落点
 *
 * 做什么：只存 W/H；不读 CFG。
 * 谁调用：GuiInitialize；WindowSetFb（二次同步）。
 * 前后文：前 — HalVideoGetSize；后 — LayoutLoadConfiguration / LayoutResolveWindow。
 */
void LayoutSetFb(UINT32 W, UINT32 H) {
    gFbW = W;
    gFbH = H;
}

/*
 * LayoutFbW — 上次 LayoutSetFb 的宽度
 *
 * 谁调用：暂无外部；调试或将来自适应 UI 可用。
 */
UINT32 LayoutFbW(void) {
    return gFbW;
}

/*
 * LayoutFbH — 上次 LayoutSetFb 的高度
 *
 * 谁调用：暂无外部；调试或将来自适应 UI 可用。
 */
UINT32 LayoutFbH(void) {
    return gFbH;
}

/*
 * LayoutBarH — 顶栏/taskbar 高度（缩放后，最小 20）
 *
 * 谁调用：StartPaintBar；WindowPaintDesktopFramebuffer / WindowPaintErase。
 */
UINT32 LayoutBarH(void) {
    UINT32 H = ScaleDesign(gBarH);
    return (H < 20u) ? 20u : H;
}

/*
 * LayoutTitleH — 窗标题栏高度（缩放后，最小 20）
 *
 * 谁调用：WindowInTitle；WindowPaintFrame；WindowShellClientRect。
 */
UINT32 LayoutTitleH(void) {
    UINT32 H = ScaleDesign(gTitleH);
    return (H < 20u) ? 20u : H;
}

/*
 * LayoutCloseW — 关闭钮最小宽度（缩放后，最小 16）
 *
 * 谁调用：WindowPaintCloseButton。
 */
UINT32 LayoutCloseW(void) {
    UINT32 W = ScaleDesign(gCloseW);
    return (W < 16u) ? 16u : W;
}

/*
 * LayoutContentTop — 可放窗/桌面的 Y 起点（顶栏底边）
 *
 * 谁调用：LayoutResolveWindow；Window ClampPos；Pointer 点空白收焦点。
 */
UINT32 LayoutContentTop(void) {
    return LayoutBarH();
}

/*
 * LayoutContentBottom — 内容区底边 Y（底栏顶边，不含底栏）
 *
 * 谁调用：LayoutResolveWindow；LayoutStartButton；StartPaintBar；WindowPaintErase。
 */
UINT32 LayoutContentBottom(void) {
    UINT32 Bar = LayoutBarH();
    return (gFbH > Bar) ? (gFbH - Bar) : 0;
}

/*
 * LayoutPx — 任意设计像素→屏像素
 *
 * 谁调用：Desktop / Start / WindowPaint 排版间距。
 */
UINT32 LayoutPx(UINT32 DesignPx) {
    return ScaleDesign(DesignPx);
}

/*
 * LayoutWindowDescription — 窗槽内建描述行（只读表项）
 *
 * 谁调用：WindowLayoutAll。
 * 返回：非法 Id 为 0。
 */
const GUI_WIN_LAYOUT *LayoutWindowDescription(int WindowId) {
    if (WindowId < 0 || WindowId >= GUI_WIN_COUNT) {
        return 0;
    }
    return &gWin[WindowId];
}

/*
 * LayoutResolveWindow — 按 Place 规则算窗矩形并夹进 FB
 *
 * 做什么：缩放 DesignW/H、尊重 MinW/H；SHELL_DELTA 先递归 shell。
 * 谁调用：WindowLayoutAll。
 * 前后文：前 — LayoutSetFb + 可选 LayoutLoadConfiguration；后 — Window 存 gWindows[]。
 */
void LayoutResolveWindow(int WindowId, UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H) {
    const GUI_WIN_LAYOUT *D;
    UINT32 Ww;
    UINT32 Hh;
    UINT32 Top;
    UINT32 Bot;
    UINT32 WorkH;
    UINT32 Sx = 0;
    UINT32 Sy = 0;
    UINT32 Sw = 0;
    UINT32 Sh = 0;

    if (X == 0 || Y == 0 || W == 0 || H == 0) {
        return;
    }
    D = LayoutWindowDescription(WindowId);
    if (D == 0) {
        *X = *Y = 0;
        *W = *H = 0;
        return;
    }
    Top = LayoutContentTop();
    Bot = LayoutContentBottom();
    WorkH = (Bot > Top) ? (Bot - Top) : gFbH;

    Ww = ScaleDesign(D->DesignW);
    Hh = ScaleDesign(D->DesignH);
    if (Ww < D->MinW) {
        Ww = D->MinW;
    }
    if (Hh < D->MinH) {
        Hh = D->MinH;
    }
    if (Ww + 16u > gFbW) {
        Ww = (gFbW > 32u) ? (gFbW - 16u) : gFbW;
    }
    if (Hh + 16u > WorkH) {
        Hh = (WorkH > 32u) ? (WorkH - 16u) : ((WorkH > 0) ? WorkH : Hh);
    }

    if (D->Place == GUI_PLACE_CENTER) {
        *X = (gFbW > Ww) ? ((gFbW - Ww) / 2u) : 0;
        *Y = Top + ScaleDesign((UINT32)(D->DesignY > 0 ? D->DesignY : 0));
    } else if (D->Place == GUI_PLACE_XY) {
        *X = ScaleDesign((UINT32)(D->DesignX > 0 ? D->DesignX : 0));
        *Y = Top + ScaleDesign((UINT32)(D->DesignY > 0 ? D->DesignY : 0));
    } else {
        LayoutResolveWindow(GUI_WIN_SHELL, &Sx, &Sy, &Sw, &Sh);
        *X = Sx + ScaleDesign((UINT32)(D->DesignX > 0 ? D->DesignX : 0));
        *Y = Sy + ScaleDesign((UINT32)(D->DesignY > 0 ? D->DesignY : 0));
        (void)Sw;
        (void)Sh;
    }

    if (*X + Ww > gFbW) {
        *X = (gFbW > Ww) ? (gFbW - Ww) : 0;
    }
    if (*Y + Hh > Bot) {
        *Y = (Bot > Hh) ? (Bot - Hh) : Top;
    }
    if (*Y < Top) {
        *Y = Top;
    }
    *W = Ww;
    *H = Hh;
}

/*
 * LayoutIconTile — 桌面图标色块边长（缩放后，最小 24）
 *
 * 谁调用：LayoutIconSlot；DesktopPaintIcons。
 */
UINT32 LayoutIconTile(void) {
    UINT32 T = ScaleDesign(gIconTile);
    return (T < 24u) ? 24u : T;
}

/*
 * LayoutIconSlot — 第 Slot 个桌面图标外接矩形
 *
 * 谁调用：Desktop IconGeometry / DesktopHitIcon。
 */
void LayoutIconSlot(int Slot, UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H) {
    UINT32 Tile = LayoutIconTile();
    UINT32 Gap = ScaleDesign(gIconGap);
    UINT32 Lab = ScaleDesign(gIconLabelH);
    UINT32 Stride = ScaleDesign(gIconStride);

    if (Slot < 0) {
        Slot = 0;
    }
    *X = ScaleDesign(gIconX0) + (UINT32)Slot * Stride;
    *Y = ScaleDesign(gIconY0);
    if (*Y < LayoutContentTop()) {
        *Y = LayoutContentTop() + ScaleDesign(20u);
    }
    *W = Tile + ScaleDesign(24u);
    *H = Tile + Gap + Lab;
}

/*
 * LayoutStartButton — 底栏「开始」钮矩形
 *
 * 谁调用：StartPaintBar / StartHitButton；LayoutStartMenu。
 */
void LayoutStartButton(UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H) {
    UINT32 BarY = LayoutContentBottom();
    *X = ScaleDesign(gStartButtonX);
    *Y = BarY + ScaleDesign(gStartPadY);
    *W = ScaleDesign(gStartButtonWidth);
    *H = ScaleDesign(gStartButtonHeight);
    if (*W < 72u) {
        *W = 72u;
    }
    if (*H < 16u) {
        *H = 16u;
    }
}

/*
 * LayoutStartMenu — 开始弹出菜单外框（贴底栏上方）
 *
 * 谁调用：StartPaintMenu / StartHitMenu。
 */
void LayoutStartMenu(UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H) {
    UINT32 Bx;
    UINT32 By;
    UINT32 Bw;
    UINT32 Bh;
    UINT32 Mh;

    LayoutStartButton(&Bx, &By, &Bw, &Bh);
    Mh = gMenuN * ScaleDesign(gMenuRow) + ScaleDesign(8u);
    *X = Bx;
    *W = ScaleDesign(gMenuW);
    *H = Mh;
    *Y = (LayoutContentBottom() > Mh) ? (LayoutContentBottom() - Mh) : 0;
    (void)By;
    (void)Bh;
}
