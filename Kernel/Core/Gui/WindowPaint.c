/*
 * WindowPaint.c — 桌面底图、窗框、擦除与拖窗脏 Present
 *
 * 【初学者】
 * - 分层：Core/Gui；Window.c 调本文件；勿在此做命中/开闭窗
 * - 对外：WindowPaint*（WindowPaint.h）
 * - 客户区委托 Console / Settings / Files / StoreUi
 */
#include "WindowPaint.h"
#include "Console.h"
#include "Font.h"
#include "Files.h"
#include "Layout.h"
#include "Settings.h"
#include "StoreUi.h"
#include "Start.h"
#include "Window.h"
#include "HalVideo.h"
#include "Locale.h"
#include "Theme.h"
#include "Utf8.h"

#define GUI_CLOSE_BACKGROUND 0x00B33A3Au
#define WINDOW_MOVE_PRESENT_PAD 2u

/*
 * WindowPaintCloseButton — 算标题栏右上关闭钮矩形
 *
 * 做什么：高随 LayoutTitleH；宽不小于 LayoutCloseW。
 * 谁调用：WindowPaintFrame；WindowInClose（WindowManage）。
 * 前后文：兄弟 — PaintCloseX 画叉线。
 */
void WindowPaintCloseButton(UINT32 WinX, UINT32 WinY, UINT32 WinW,
                          UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H) {
    UINT32 TitleH = LayoutTitleH();
    *H = (TitleH > 8u) ? (TitleH - 6u) : TitleH;
    *W = *H;
    if (*W < LayoutCloseW()) {
        *W = LayoutCloseW();
    }
    *X = WinX + WinW - LayoutPx(6u) - *W;
    *Y = WinY + (TitleH > *H ? (TitleH - *H) / 2u : 0) + 1u;
}

/*
 * PaintCloseX — 关闭钮内两段 Bresenham 对角线
 *
 * 谁调用：WindowPaintFrame。
 */
static void PaintCloseX(UINT32 Bx, UINT32 By, UINT32 Bw, UINT32 Bh,
                        UINT32 Color) {
    UINT32 Pad;
    UINT32 X0;
    UINT32 Y0;
    UINT32 X1;
    UINT32 Y1;
    INT32 Dx;
    INT32 Dy;
    INT32 Sx;
    INT32 Sy;
    INT32 Err;
    INT32 E2;
    INT32 Cx;
    INT32 Cy;

    Pad = (Bw > 10u && Bh > 10u) ? 4u : 2u;
    if (Bw <= Pad * 2u + 2u || Bh <= Pad * 2u + 2u) {
        Pad = 1u;
    }
    X0 = Bx + Pad;
    Y0 = By + Pad;
    X1 = Bx + Bw - 1u - Pad;
    Y1 = By + Bh - 1u - Pad;

    Dx = (INT32)X1 - (INT32)X0;
    Dy = (INT32)Y1 - (INT32)Y0;
    if (Dx < 0) {
        Dx = -Dx;
    }
    if (Dy < 0) {
        Dy = -Dy;
    }
    Sx = ((INT32)X0 < (INT32)X1) ? 1 : -1;
    Sy = ((INT32)Y0 < (INT32)Y1) ? 1 : -1;
    Err = Dx - Dy;
    Cx = (INT32)X0;
    Cy = (INT32)Y0;
    for (;;) {
        HalVideoDrawPixel((UINT32)Cx, (UINT32)Cy, Color);
        if (Cx == (INT32)X1 && Cy == (INT32)Y1) {
            break;
        }
        E2 = Err * 2;
        if (E2 > -Dy) {
            Err -= Dy;
            Cx += Sx;
        }
        if (E2 < Dx) {
            Err += Dx;
            Cy += Sy;
        }
    }

    X0 = Bx + Bw - 1u - Pad;
    Y0 = By + Pad;
    X1 = Bx + Pad;
    Y1 = By + Bh - 1u - Pad;
    Dx = (INT32)X1 - (INT32)X0;
    Dy = (INT32)Y1 - (INT32)Y0;
    if (Dx < 0) {
        Dx = -Dx;
    }
    if (Dy < 0) {
        Dy = -Dy;
    }
    Sx = ((INT32)X0 < (INT32)X1) ? 1 : -1;
    Sy = ((INT32)Y0 < (INT32)Y1) ? 1 : -1;
    Err = Dx - Dy;
    Cx = (INT32)X0;
    Cy = (INT32)Y0;
    for (;;) {
        HalVideoDrawPixel((UINT32)Cx, (UINT32)Cy, Color);
        if (Cx == (INT32)X1 && Cy == (INT32)Y1) {
            break;
        }
        E2 = Err * 2;
        if (E2 > -Dy) {
            Err -= Dy;
            Cx += Sx;
        }
        if (E2 < Dx) {
            Err += Dx;
            Cy += Sy;
        }
    }
}

/*
 * WindowPaintDesktopFramebuffer — 整屏桌面色 + 顶栏 + 底栏开始条
 *
 * 做什么：不画窗；StartPaintBar 补底栏（与 WindowCompose 一致）。
 * 谁调用：WindowPaintDesktop；GuiRefreshLabels 路径。
 * 前后文：后 — WindowCompose 叠窗与图标。
 */
void WindowPaintDesktopFramebuffer(UINT32 FbW, UINT32 FbH) {
    UINT32 BarH = LayoutBarH();
    HalVideoFillRect(0, 0, FbW, FbH, ThemeDesktopBackground());
    HalVideoFillRect(0, 0, FbW, BarH, ThemeTaskbarBackground());
    FontDrawStringAt(LayoutPx(12u), LayoutPx(8u), LocStr(MSG_DESKTOP_TITLE),
                     ThemeWindowTitleText());
    StartPaintBar();
}

/*
 * WindowPaintErase — 用桌面色填窗旧矩形（含拖影 padding）
 *
 * 做什么：若擦到顶栏/底栏区域则局部重画栏与标题字。
 * 谁调用：WindowClose；WindowMoveTo（WindowManage）。
 * 前后文：后 — WindowCompose 全帧合成。
 */
void WindowPaintErase(UINT32 FbW, UINT32 FbH, UINT32 X, UINT32 Y, UINT32 W,
                       UINT32 H) {
    UINT32 L = (X > WINDOW_MOVE_PRESENT_PAD) ? (X - WINDOW_MOVE_PRESENT_PAD) : 0;
    UINT32 T = (Y > WINDOW_MOVE_PRESENT_PAD) ? (Y - WINDOW_MOVE_PRESENT_PAD) : 0;
    UINT32 R = X + W + WINDOW_MOVE_PRESENT_PAD;
    UINT32 B = Y + H + WINDOW_MOVE_PRESENT_PAD;
    UINT32 BarH = LayoutBarH();
    UINT32 BotY = LayoutContentBottom();

    if (R > FbW) {
        R = FbW;
    }
    if (B > FbH) {
        B = FbH;
    }
    if (R > L && B > T) {
        HalVideoFillRect(L, T, R - L, B - T, ThemeDesktopBackground());
    }
    if (T < BarH) {
        HalVideoFillRect(0, 0, FbW, BarH, ThemeTaskbarBackground());
        FontDrawStringAt(LayoutPx(12u), LayoutPx(8u),
                         LocStr(MSG_DESKTOP_TITLE), ThemeWindowTitleText());
    }
    if (B > BotY) {
        StartPaintBar();
    }
}

/*
 * WindowPaintPresentMove — 拖窗后 Present 旧+新矩形并集（减全屏 flip）
 *
 * 谁调用：WindowMoveTo。
 */
void WindowPaintPresentMove(UINT32 FbW, UINT32 FbH, UINT32 X0, UINT32 Y0,
                             UINT32 W0, UINT32 H0, UINT32 X1, UINT32 Y1,
                             UINT32 W1, UINT32 H1) {
    UINT32 L = (X0 < X1) ? X0 : X1;
    UINT32 T = (Y0 < Y1) ? Y0 : Y1;
    UINT32 R0 = X0 + W0;
    UINT32 R1 = X1 + W1;
    UINT32 B0 = Y0 + H0;
    UINT32 B1 = Y1 + H1;
    UINT32 R = (R0 > R1) ? R0 : R1;
    UINT32 B = (B0 > B1) ? B0 : B1;

    if (L > WINDOW_MOVE_PRESENT_PAD) {
        L -= WINDOW_MOVE_PRESENT_PAD;
    } else {
        L = 0;
    }
    if (T > WINDOW_MOVE_PRESENT_PAD) {
        T -= WINDOW_MOVE_PRESENT_PAD;
    } else {
        T = 0;
    }
    R += WINDOW_MOVE_PRESENT_PAD;
    B += WINDOW_MOVE_PRESENT_PAD;
    if (R > FbW) {
        R = FbW;
    }
    if (B > FbH) {
        B = FbH;
    }
    if (R > L && B > T) {
        HalVideoPresentRect(L, T, R - L, B - T);
    }
}

/*
 * DrawClippedAt — 客户区 UTF-8 串，右/下越界即停
 *
 * 谁调用：WindowPaintFrame（About 文案）。
 */
static void DrawClippedAt(UINT32 X, UINT32 Y, UINT32 MaxX, UINT32 MaxY,
                          const char *Text, UINT32 Color) {
    UINT32 Cursor = X;
    UINT32 CellH = FontCellHeight();

    if (Text == 0 || Y + CellH > MaxY) {
        return;
    }
    while (*Text) {
        UINT32 Cp;
        UINTN N;
        UINT32 Gw;

        if (*Text == '\n') {
            break;
        }
        N = Utf8Decode(Text, &Cp);
        if (N == 0) {
            Text++;
            continue;
        }
        Gw = (Cp < 0x80u) ? FontCellWidth() : FontCjkCell();
        if (Cursor + Gw > MaxX) {
            break;
        }
        FontDrawCodepointAt(Cursor, Y, Cp, Color);
        Cursor += Gw;
        Text += N;
    }
}

/*
 * WindowPaintFrame — 边框、标题、关闭钮与各窗客户区
 *
 * 做什么：Kind=GUI_WIN_* 时分发 Console/Settings/Files/StoreUi/About。
 * 谁调用：Window.c PaintOne。
 * 前后文：前 — LayoutTitleH；兄弟 — WindowPaintCloseButton / PaintCloseX。
 */
void WindowPaintFrame(UINT32 X, UINT32 Y, UINT32 W, UINT32 H, int Focus,
                       LOC_MSG Title, int Kind) {
    UINT32 TitleBg;
    UINT32 Bx;
    UINT32 By;
    UINT32 Bw;
    UINT32 Bh;
    UINT32 Cx;
    UINT32 Cy;
    UINT32 Cw;
    UINT32 Ch;

    {
        UINT32 TitleH = LayoutTitleH();
        HalVideoFillRect(X, Y, W, H, ThemeWindowBorder());
        TitleBg = Focus ? ThemeWindowTitleBar() : ThemeWindowTitleBarDim();
        HalVideoFillRect(X + 1u, Y + 1u, W - 2u, TitleH, TitleBg);
        WindowPaintCloseButton(X, Y, W, &Bx, &By, &Bw, &Bh);
        DrawClippedAt(X + 10u, Y + 5u, Bx > 4u ? Bx - 4u : X + 10u, Y + TitleH,
                      LocStr(Title), ThemeWindowTitleText());
        HalVideoFillRect(Bx, By, Bw, Bh, GUI_CLOSE_BACKGROUND);
        PaintCloseX(Bx, By, Bw, Bh, ThemeWindowTitleText());
        Cx = X + 1u;
        Cy = Y + 1u + TitleH;
        Cw = W - 2u;
        Ch = (H > TitleH + 2u) ? (H - 2u - TitleH) : 0;
    }
    if (Ch > 0) {
        HalVideoFillRect(Cx, Cy, Cw, Ch, ThemeWindowClient());
    }
    if (Kind == GUI_WIN_SHELL) {
        ConsolePaintBannerBack();
    } else if (Kind == GUI_WIN_SETTINGS && Ch > 0) {
        SettingsPaintClient(Cx, Cy, Cw, Ch);
    } else if (Kind == GUI_WIN_FILES && Ch > 0) {
        FilesPaintClient(Cx, Cy, Cw, Ch);
    } else if (Kind == GUI_WIN_STORE && Ch > 0) {
        StoreUiPaintClient(Cx, Cy, Cw, Ch);
    } else if (Kind == GUI_WIN_ABOUT && Ch > 0) {
        DrawClippedAt(Cx + 12u, Cy + 16u, Cx + Cw - 4u, Cy + Ch,
                      LocStr(MSG_ABOUT_BODY), ThemeWindowTitleText());
    }
}
