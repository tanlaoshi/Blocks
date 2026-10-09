/*
 * GuiDesktop.c — K33 Shell 图标 · K34 Settings 图标（双击开/聚焦）
 */
#include "GuiDesktop.h"
#include "Font.h"
#include "GuiLayout.h"
#include "GuiWin.h"
#include "HalSerial.h"
#include "HalTimer.h"
#include "HalVideo.h"
#include "Locale.h"
#include "Theme.h"
#include "ToySerialConfig.h"

#define DBL_TIMER_TICKS 50u
#define DBL_SOFT_TICKS  400000u
#define DBL_SLOP        12

static UINT32 gFbW;
static UINT32 gFbH;
static UINT32 gSoftTick;
static UINT32 gLastClickTick;
static INT32 gLastClickX = -1000;
static INT32 gLastClickY = -1000;
static int gLastWin = -1;
static int gHaveLastClick;

static UINT32 NowTick(void) {
    if (HalTimerReady()) {
        return (UINT32)HalTimerTicks();
    }
    return gSoftTick;
}

static void IconGeom(int Slot, UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H) {
    GuiLayoutIconSlot(Slot, X, Y, W, H);
}

static void PaintOneIcon(int Slot, UINT32 Face, LOC_MSG LabelId) {
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Tile = GuiLayoutIconTile();
    UINT32 Pad = GuiLayoutPx(12u);
    UINT32 Gap = GuiLayoutPx(6u);

    IconGeom(Slot, &X, &Y, &W, &H);
    HalVideoFillRect(X + Pad, Y, Tile, Tile, Face);
    HalVideoFillRect(X + Pad + 4u, Y + 4u, Tile - 8u, Tile - 8u,
                     ThemeWindowClient());
    FontDrawStringAt(X + GuiLayoutPx(8u), Y + Tile + Gap, LocStr(LabelId),
                     ThemeWindowTitleText());
    (void)W;
    (void)H;
}

void GuiDesktopSetFb(UINT32 W, UINT32 H) {
    gFbW = W;
    gFbH = H;
    gSoftTick = 0;
    gHaveLastClick = 0;
    gLastWin = -1;
}

void GuiDesktopPollTick(void) {
    gSoftTick++;
}

void GuiDesktopPaintIcons(void) {
    if (gFbW < 160u || gFbH < 80u) {
        return;
    }
    PaintOneIcon(0, ThemeWindowTitleBar(), MSG_ICON_SHELL);
    PaintOneIcon(1, 0x00507040u, MSG_ICON_SETTINGS);
    PaintOneIcon(2, 0x00406080u, MSG_ICON_FILES);
}

int GuiDesktopHitIcon(INT32 X, INT32 Y) {
    UINT32 Ix;
    UINT32 Iy;
    UINT32 Iw;
    UINT32 Ih;
    int Slot;
    static const int Map[3] = { GUI_WIN_SHELL, GUI_WIN_SETTINGS, GUI_WIN_FILES };

    for (Slot = 0; Slot < 3; Slot++) {
        IconGeom(Slot, &Ix, &Iy, &Iw, &Ih);
        if (X >= (INT32)Ix && Y >= (INT32)Iy && X < (INT32)(Ix + Iw) &&
            Y < (INT32)(Iy + Ih)) {
            return Map[Slot];
        }
    }
    return -1;
}

static void Activate(int WinId) {
    if (GuiWinIsOn(WinId)) {
        GuiWinFocus(WinId);
        HalSerialWriteChannel(TOY_SLOG_GUI, "Gui: icon focus\n");
    } else {
        GuiWinOpen(WinId);
        HalSerialWriteChannel(TOY_SLOG_GUI, "Gui: icon open\n");
    }
}

int GuiDesktopClickIcon(int WinId, INT32 X, INT32 Y) {
    UINT32 Now;
    INT32 Dx;
    INT32 Dy;
    int IsDbl = 0;

    if (WinId != GUI_WIN_SHELL && WinId != GUI_WIN_SETTINGS &&
        WinId != GUI_WIN_FILES) {
        return 0;
    }
    Now = NowTick();
    if (gHaveLastClick && gLastWin == WinId) {
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
    gLastWin = WinId;
    gHaveLastClick = 1;
    if (IsDbl) {
        gHaveLastClick = 0;
        Activate(WinId);
        return 1;
    }
    return 0;
}
