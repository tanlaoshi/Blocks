/*
 * Desktop.c — 桌面图标：绘制、命中、双击开窗/聚焦
 *
 * 【初学者】
 * - 分层：Core/Gui；几何来自 LayoutIconSlot
 * - 对外：DesktopSetFb / DesktopPollTick / DesktopPaintIcons /
 *         DesktopHitIcon / DesktopClickIcon
 * - 不做：窗客户区内容（Files/Settings/…）
 */
#include "Desktop.h"
#include "Font.h"
#include "Layout.h"
#include "Window.h"
#include "HalSerial.h"
#include "HalTimer.h"
#include "HalVideo.h"
#include "Locale.h"
#include "Theme.h"
#include "SerialConfig.h"
#include "Utf8.h"

#define DOUBLE_CLICK_TIMER_TICKS 50u
#define DOUBLE_CLICK_SOFT_TICKS  400000u
#define DOUBLE_CLICK_SLOP_PIXELS 12

static UINT32 gFbW;
static UINT32 gFbH;
static UINT32 gSoftTick;
static UINT32 gLastClickTick;
static INT32 gLastClickX = -1000;
static INT32 gLastClickY = -1000;
static int gLastWindowId = -1;
static int gHaveLastClick;

/*
 * NowTick — 双击判定用时间基
 *
 * 谁调用：仅 DesktopClickIcon。优先硬件定时器，否则软计数。
 */
static UINT32 NowTick(void) {
    if (HalTimerReady()) {
        return (UINT32)HalTimerTicks();
    }
    return gSoftTick;
}

/*
 * IconGeometry — 取第 Slot 个图标矩形
 *
 * 谁调用：PaintOneIcon / DesktopHitIcon。
 */
static void IconGeometry(int Slot, UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H) {
    LayoutIconSlot(Slot, X, Y, W, H);
}

/*
 * LabelPixelWidth — 标签像素宽（与 FontDrawStringAt 步进一致）
 *
 * 谁调用：仅 PaintOneIcon（居中）。
 */
static UINT32 LabelPixelWidth(const char *Text) {
    UINT32 Width = 0;

    if (Text == 0) {
        return 0;
    }
    while (*Text) {
        UINT32 CodePoint;
        UINTN Advance;

        if (*Text == '\n') {
            break;
        }
        Advance = Utf8Decode(Text, &CodePoint);
        if (Advance == 0) {
            Text++;
            continue;
        }
        Width += (CodePoint < 0x80u) ? FontCellWidth() : FontCjkCell();
        Text += Advance;
    }
    return Width;
}

/*
 * PaintOneIcon — 画单个图标色块 + 居中标签
 *
 * 谁调用：仅 DesktopPaintIcons。
 */
static void PaintOneIcon(int Slot, UINT32 Face, LOC_MSG LabelId) {
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Tile = LayoutIconTile();
    UINT32 Pad = LayoutPx(12u);
    UINT32 Gap = LayoutPx(6u);
    const char *Lab;
    UINT32 TextWidth;
    UINT32 FaceLeft;
    UINT32 TextX;

    IconGeometry(Slot, &X, &Y, &W, &H);
    HalVideoFillRect(X + Pad, Y, Tile, Tile, Face);
    HalVideoFillRect(X + Pad + 4u, Y + 4u, Tile - 8u, Tile - 8u,
                     ThemeWindowClient());
    Lab = LocStr(LabelId);
    TextWidth = LabelPixelWidth(Lab);
    FaceLeft = X + Pad;
    if (TextWidth <= Tile) {
        TextX = FaceLeft + (Tile - TextWidth) / 2u;
    } else if (TextWidth / 2u <= FaceLeft + Tile / 2u) {
        TextX = FaceLeft + Tile / 2u - TextWidth / 2u;
    } else {
        TextX = X;
    }
    FontDrawStringAt(TextX, Y + Tile + Gap, Lab, ThemeWindowTitleText());
    (void)W;
    (void)H;
}

/*
 * DesktopSetFb — 记录分辨率并清双击状态
 *
 * 谁调用：GuiInitialize。
 */
void DesktopSetFb(UINT32 W, UINT32 H) {
    gFbW = W;
    gFbH = H;
    gSoftTick = 0;
    gHaveLastClick = 0;
    gLastWindowId = -1;
}

/*
 * DesktopPollTick — 无硬件定时器时推进软时钟
 *
 * 谁调用：GuiPoll 每帧。
 */
void DesktopPollTick(void) {
    gSoftTick++;
}

/*
 * DesktopPaintIcons — 画 Shell/Settings/Files/Store 四图标
 *
 * 谁调用：Window 桌面绘制路径。
 */
void DesktopPaintIcons(void) {
    if (gFbW < 160u || gFbH < 80u) {
        return;
    }
    PaintOneIcon(0, ThemeWindowTitleBar(), MSG_ICON_SHELL);
    PaintOneIcon(1, 0x00507040u, MSG_ICON_SETTINGS);
    PaintOneIcon(2, 0x00406080u, MSG_ICON_FILES);
    PaintOneIcon(3, 0x00705030u, MSG_ICON_STORE);
}

/*
 * DesktopHitIcon — 坐标落在哪个窗图标上
 *
 * 谁调用：GuiPoll 桌面空白点击前。
 * 返回：GUI_WIN_* 或 -1
 */
int DesktopHitIcon(INT32 X, INT32 Y) {
    UINT32 Ix;
    UINT32 Iy;
    UINT32 Iw;
    UINT32 Ih;
    int Slot;
    static const int Map[4] = {
        GUI_WIN_SHELL, GUI_WIN_SETTINGS, GUI_WIN_FILES, GUI_WIN_STORE
    };

    for (Slot = 0; Slot < 4; Slot++) {
        IconGeometry(Slot, &Ix, &Iy, &Iw, &Ih);
        if (X >= (INT32)Ix && Y >= (INT32)Iy && X < (INT32)(Ix + Iw) &&
            Y < (INT32)(Iy + Ih)) {
            return Map[Slot];
        }
    }
    return -1;
}

/*
 * ActivateWindowFromIcon — 开窗或聚焦
 *
 * 谁调用：仅 DesktopClickIcon（双击成功）。
 */
static void ActivateWindowFromIcon(int WindowId) {
    if (WindowIsOn(WindowId)) {
        WindowFocus(WindowId);
        HalSerialWriteChannel(SLOG_GUI, "Gui: icon focus\n");
    } else {
        WindowOpen(WindowId);
        HalSerialWriteChannel(SLOG_GUI, "Gui: icon open\n");
    }
}

/*
 * DesktopClickIcon — 图标单击记点 / 双击开窗
 *
 * 谁调用：GuiPoll（DesktopHitIcon >= 0 时）。
 * 返回：1 双击已开窗需重画；0 仅记录单击
 */
int DesktopClickIcon(int WindowId, INT32 X, INT32 Y) {
    UINT32 Now;
    INT32 Dx;
    INT32 Dy;
    int IsDouble = 0;

    if (WindowId != GUI_WIN_SHELL && WindowId != GUI_WIN_SETTINGS &&
        WindowId != GUI_WIN_FILES && WindowId != GUI_WIN_STORE) {
        return 0;
    }
    Now = NowTick();
    if (gHaveLastClick && gLastWindowId == WindowId) {
        Dx = X - gLastClickX;
        Dy = Y - gLastClickY;
        if (Dx < 0) {
            Dx = -Dx;
        }
        if (Dy < 0) {
            Dy = -Dy;
        }
        if (Dx <= DOUBLE_CLICK_SLOP_PIXELS && Dy <= DOUBLE_CLICK_SLOP_PIXELS) {
            UINT32 Span = Now - gLastClickTick;
            if (HalTimerReady()) {
                IsDouble = (Span <= DOUBLE_CLICK_TIMER_TICKS);
            } else {
                IsDouble = (Span <= DOUBLE_CLICK_SOFT_TICKS);
            }
        }
    }
    gLastClickTick = Now;
    gLastClickX = X;
    gLastClickY = Y;
    gLastWindowId = WindowId;
    gHaveLastClick = 1;
    if (IsDouble) {
        gHaveLastClick = 0;
        ActivateWindowFromIcon(WindowId);
        return 1;
    }
    return 0;
}
