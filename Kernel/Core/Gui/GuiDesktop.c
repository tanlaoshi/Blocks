/*
 * GuiDesktop.c — K33：桌面 Shell 图标 + 双击开/聚焦（对标 Desktop/D4 薄）
 *
 * 【初学者】
 * 不做拖图标、落盘坐标、多类型图标。Compose 前重画图标，避免拖窗擦花。
 */
#include "GuiDesktop.h"
#include "Font.h"
#include "GuiWin.h"
#include "HalSerial.h"
#include "HalTimer.h"
#include "HalVideo.h"
#include "Locale.h"
#include "Theme.h"
#include "ToySerialConfig.h"

#define GUI_BAR_H     28u
#define ICON_X        28u
#define ICON_TILE     40u
#define ICON_GAP      6u
#define ICON_LABEL_H  18u
#define DBL_TIMER_TICKS 50u     /* LAPIC 节拍窗 ≈ 半秒级 */
#define DBL_SOFT_TICKS  400000u /* 无 timer：GuiPoll 紧环次数 */
#define DBL_SLOP        12      /* 两次点击允许偏移 */

static UINT32 gFbW;
static UINT32 gFbH;
static UINT32 gSoftTick;
static UINT32 gLastClickTick;
static INT32 gLastClickX = -1000;
static INT32 gLastClickY = -1000;
static int gHaveLastClick;

static UINT32 NowTick(void) {
    if (HalTimerReady()) {
        return (UINT32)HalTimerTicks();
    }
    return gSoftTick;
}

static void IconGeom(UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H) {
    *X = ICON_X;
    *Y = GUI_BAR_H + 20u;
    *W = ICON_TILE + 24u;
    *H = ICON_TILE + ICON_GAP + ICON_LABEL_H;
}

void GuiDesktopSetFb(UINT32 W, UINT32 H) {
    gFbW = W;
    gFbH = H;
    gSoftTick = 0;
    gHaveLastClick = 0;
}

void GuiDesktopPollTick(void) {
    gSoftTick++;
}

void GuiDesktopPaintIcons(void) {
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Tx;
    UINT32 Ty;
    const char *Label;

    if (gFbW < 80u || gFbH < 80u) {
        return;
    }
    IconGeom(&X, &Y, &W, &H);
    /* 色块当图标面 */
    HalVideoFillRect(X + 12u, Y, ICON_TILE, ICON_TILE, ThemeWindowTitleBar());
    HalVideoFillRect(X + 12u + 4u, Y + 4u, ICON_TILE - 8u, ICON_TILE - 8u,
                     ThemeWindowClient());
    Label = LocStr(MSG_ICON_SHELL);
    Tx = X + 8u;
    Ty = Y + ICON_TILE + ICON_GAP;
    /* 标签短，落在图标槽宽内 */
    FontDrawStringAt(Tx, Ty, Label, ThemeWindowTitleText());
    (void)W;
    (void)H;
}

int GuiDesktopHitShell(INT32 X, INT32 Y) {
    UINT32 Ix;
    UINT32 Iy;
    UINT32 Iw;
    UINT32 Ih;

    IconGeom(&Ix, &Iy, &Iw, &Ih);
    if (X < (INT32)Ix || Y < (INT32)Iy) {
        return 0;
    }
    if (X >= (INT32)(Ix + Iw) || Y >= (INT32)(Iy + Ih)) {
        return 0;
    }
    return 1;
}

static void ActivateShell(void) {
    if (GuiWinIsOn(GUI_WIN_SHELL)) {
        GuiWinFocus(GUI_WIN_SHELL);
        HalSerialWriteChannel(TOY_SLOG_GUI, "Gui: icon focus shell\n");
    } else {
        GuiWinOpen(GUI_WIN_SHELL);
        HalSerialWriteChannel(TOY_SLOG_GUI, "Gui: icon open shell\n");
    }
}

int GuiDesktopClickShell(INT32 X, INT32 Y) {
    UINT32 Now;
    INT32 Dx;
    INT32 Dy;
    int IsDbl;

    Now = NowTick();
    IsDbl = 0;
    if (gHaveLastClick) {
        Dx = X - gLastClickX;
        Dy = Y - gLastClickY;
        if (Dx < 0) {
            Dx = -Dx;
        }
        if (Dy < 0) {
            Dy = -Dy;
        }
        if (Dx <= DBL_SLOP && Dy <= DBL_SLOP) {
            UINT32 Span = Now - gLastClickTick;
            if (HalTimerReady()) {
                IsDbl = (Span <= DBL_TIMER_TICKS);
            } else {
                IsDbl = (Span <= DBL_SOFT_TICKS);
            }
        }
    }
    gLastClickTick = Now;
    gLastClickX = X;
    gLastClickY = Y;
    gHaveLastClick = 1;
    if (IsDbl) {
        gHaveLastClick = 0;
        ActivateShell();
        return 1;
    }
    return 0;
}
