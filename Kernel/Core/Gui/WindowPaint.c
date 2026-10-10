/*
 * WindowPaint.c — K32：桌面/窗绘制与脏 Present（Window 内部）
 */
#include "WindowPaint.h"
#include "Console.h"
#include "Font.h"
#include "Files.h"
#include "Layout.h"
#include "Settings.h"
#include "Start.h"
#include "Window.h"
#include "HalVideo.h"
#include "Locale.h"
#include "Theme.h"
#include "Utf8.h"

#define GUI_CLOSE_BG 0x00B33A3Au
#define WIN_MOVE_PAD 2u

void WindowPaint_CloseBtn(UINT32 WinX, UINT32 WinY, UINT32 WinW,
                          UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H) {
    UINT32 TitleH = LayoutTitleH();
    /* 钮高随标题栏，宽至少够画几何 × */
    *H = (TitleH > 8u) ? (TitleH - 6u) : TitleH;
    *W = *H;
    if (*W < LayoutCloseW()) {
        *W = LayoutCloseW();
    }
    *X = WinX + WinW - LayoutPx(6u) - *W;
    *Y = WinY + (TitleH > *H ? (TitleH - *H) / 2u : 0) + 1u;
}

/* 红钮内居中两段对角线，避免 16px 字母「x」溢出错位 */
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

    /* 另一对角 */
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

void WindowPaint_Desktop(UINT32 FbW, UINT32 FbH) {
    UINT32 BarH = LayoutBarH();
    HalVideoFillRect(0, 0, FbW, FbH, ThemeDesktopBackground());
    HalVideoFillRect(0, 0, FbW, BarH, ThemeTaskbarBackground());
    FontDrawStringAt(LayoutPx(12u), LayoutPx(8u), LocStr(MSG_DESKTOP_TITLE),
                     ThemeWindowTitleText());
    StartPaintBar();
}

void WindowPaint_Erase(UINT32 FbW, UINT32 FbH, UINT32 X, UINT32 Y, UINT32 W,
                       UINT32 H) {
    UINT32 L = (X > WIN_MOVE_PAD) ? (X - WIN_MOVE_PAD) : 0;
    UINT32 T = (Y > WIN_MOVE_PAD) ? (Y - WIN_MOVE_PAD) : 0;
    UINT32 R = X + W + WIN_MOVE_PAD;
    UINT32 B = Y + H + WIN_MOVE_PAD;
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

void WindowPaint_PresentMove(UINT32 FbW, UINT32 FbH, UINT32 X0, UINT32 Y0,
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

    if (L > WIN_MOVE_PAD) {
        L -= WIN_MOVE_PAD;
    } else {
        L = 0;
    }
    if (T > WIN_MOVE_PAD) {
        T -= WIN_MOVE_PAD;
    } else {
        T = 0;
    }
    R += WIN_MOVE_PAD;
    B += WIN_MOVE_PAD;
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

/* 客户区内逐字画；超出右/下边界即停，避免拖窗窗外残影 */
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

void WindowPaint_Frame(UINT32 X, UINT32 Y, UINT32 W, UINT32 H, int Focus,
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
        WindowPaint_CloseBtn(X, Y, W, &Bx, &By, &Bw, &Bh);
        DrawClippedAt(X + 10u, Y + 5u, Bx > 4u ? Bx - 4u : X + 10u, Y + TitleH,
                      LocStr(Title), ThemeWindowTitleText());
        HalVideoFillRect(Bx, By, Bw, Bh, GUI_CLOSE_BG);
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
    } else if (Kind == GUI_WIN_ABOUT && Ch > 0) {
        DrawClippedAt(Cx + 12u, Cy + 16u, Cx + Cw - 4u, Cy + Ch,
                      LocStr(MSG_ABOUT_BODY), ThemeWindowTitleText());
    }
}
