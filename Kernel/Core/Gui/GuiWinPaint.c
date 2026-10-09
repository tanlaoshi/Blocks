/*
 * GuiWinPaint.c — K32：桌面/窗绘制与脏 Present（GuiWin 内部）
 */
#include "GuiWinPaint.h"
#include "Console.h"
#include "Font.h"
#include "GuiSettings.h"
#include "GuiWin.h"
#include "HalVideo.h"
#include "Locale.h"
#include "Theme.h"
#include "Utf8.h"

#define GUI_BAR_H    28u
#define GUI_TITLE_H  24u
#define GUI_CLOSE_W  20u
#define GUI_CLOSE_BG 0x00B33A3Au
#define WIN_MOVE_PAD 2u

void GuiWinPaint_CloseBtn(UINT32 WinX, UINT32 WinY, UINT32 WinW,
                          UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H) {
    *W = GUI_CLOSE_W;
    *H = GUI_TITLE_H - 6u;
    *X = WinX + WinW - 6u - *W;
    *Y = WinY + 3u;
}

void GuiWinPaint_Desktop(UINT32 FbW, UINT32 FbH) {
    HalVideoFillRect(0, 0, FbW, FbH, ThemeDesktopBackground());
    HalVideoFillRect(0, 0, FbW, GUI_BAR_H, ThemeTaskbarBackground());
    FontDrawStringAt(12, 8, LocStr(MSG_DESKTOP_TITLE), ThemeWindowTitleText());
}

void GuiWinPaint_Erase(UINT32 FbW, UINT32 FbH, UINT32 X, UINT32 Y, UINT32 W,
                       UINT32 H) {
    UINT32 L = (X > WIN_MOVE_PAD) ? (X - WIN_MOVE_PAD) : 0;
    UINT32 T = (Y > WIN_MOVE_PAD) ? (Y - WIN_MOVE_PAD) : 0;
    UINT32 R = X + W + WIN_MOVE_PAD;
    UINT32 B = Y + H + WIN_MOVE_PAD;

    if (R > FbW) {
        R = FbW;
    }
    if (B > FbH) {
        B = FbH;
    }
    if (R > L && B > T) {
        HalVideoFillRect(L, T, R - L, B - T, ThemeDesktopBackground());
    }
    if (T < GUI_BAR_H) {
        HalVideoFillRect(0, 0, FbW, GUI_BAR_H, ThemeTaskbarBackground());
        FontDrawStringAt(12, 8, LocStr(MSG_DESKTOP_TITLE), ThemeWindowTitleText());
    }
}

void GuiWinPaint_PresentMove(UINT32 FbW, UINT32 FbH, UINT32 X0, UINT32 Y0,
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

void GuiWinPaint_Frame(UINT32 X, UINT32 Y, UINT32 W, UINT32 H, int Focus,
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

    HalVideoFillRect(X, Y, W, H, ThemeWindowBorder());
    TitleBg = Focus ? ThemeWindowTitleBar() : ThemeWindowTitleBarDim();
    HalVideoFillRect(X + 1u, Y + 1u, W - 2u, GUI_TITLE_H, TitleBg);
    GuiWinPaint_CloseBtn(X, Y, W, &Bx, &By, &Bw, &Bh);
    /* 标题止于 × 左缘，勿画进关闭钮/窗外 */
    DrawClippedAt(X + 10u, Y + 5u, Bx > 4u ? Bx - 4u : X + 10u, Y + GUI_TITLE_H,
                  LocStr(Title), ThemeWindowTitleText());
    HalVideoFillRect(Bx, By, Bw, Bh, GUI_CLOSE_BG);
    FontDrawStringAt(Bx + 6u, By + 4u, "x", ThemeWindowTitleText());
    Cx = X + 1u;
    Cy = Y + 1u + GUI_TITLE_H;
    Cw = W - 2u;
    Ch = (H > GUI_TITLE_H + 2u) ? (H - 2u - GUI_TITLE_H) : 0;
    if (Ch > 0) {
        HalVideoFillRect(Cx, Cy, Cw, Ch, ThemeWindowClient());
    }
    if (Kind == GUI_WIN_SHELL) {
        ConsolePaintBannerBack();
    } else if (Kind == GUI_WIN_SETTINGS && Ch > 0) {
        GuiSettingsPaintClient(Cx, Cy, Cw, Ch);
    } else if (Kind == GUI_WIN_ABOUT && Ch > 0) {
        DrawClippedAt(Cx + 12u, Cy + 16u, Cx + Cw - 4u, Cy + Ch,
                      LocStr(MSG_ABOUT_BODY), ThemeWindowTitleText());
    }
}
