/*
 * GuiWin.c — K32：Shell + About 双窗，Z 序/命中/开关/拖（对标现网 Raise 薄）
 *
 * 【初学者】gZ[0]=底、末槽=顶；画底→顶，点顶→底。重叠无 alpha。
 */
#include "GuiWin.h"
#include "GuiWinPaint.h"
#include "GuiDesktop.h"
#include "Console.h"
#include "HalPs2.h"
#include "HalSerial.h"
#include "HalVideo.h"
#include "Locale.h"
#include "ToySerialConfig.h"

#define GUI_BAR_H   28u
#define GUI_TITLE_H 24u

typedef struct {
    int On;
    int Focus;
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    LOC_MSG Title;
} GUI_WIN;

static GUI_WIN gW[GUI_WIN_COUNT];
static UINT8 gZ[GUI_WIN_COUNT];
static UINT32 gFbW;
static UINT32 gFbH;

static void PaintOne(int Id) {
    if (!gW[Id].On) {
        return;
    }
    GuiWinPaint_Frame(gW[Id].X, gW[Id].Y, gW[Id].W, gW[Id].H, gW[Id].Focus,
                      gW[Id].Title, Id);
}

static void ClampPos(int Id, INT32 *X, INT32 *Y) {
    INT32 MinY = (INT32)GUI_BAR_H;
    INT32 MaxX = (INT32)gFbW - (INT32)gW[Id].W;
    INT32 MaxY = (INT32)gFbH - (INT32)gW[Id].H;

    if (*X < 0) {
        *X = 0;
    }
    if (*Y < MinY) {
        *Y = MinY;
    }
    if (MaxX < 0) {
        MaxX = 0;
    }
    if (MaxY < MinY) {
        MaxY = MinY;
    }
    if (*X > MaxX) {
        *X = MaxX;
    }
    if (*Y > MaxY) {
        *Y = MaxY;
    }
}

void GuiWinSetFb(UINT32 W, UINT32 H) {
    gFbW = W;
    gFbH = H;
}

void GuiWinPaintDesktop(void) {
    GuiWinPaint_Desktop(gFbW, gFbH);
}

void GuiWinCompose(void) {
    int i;
    /* 先图标后窗：拖/关擦桌面后图标不会丢 */
    GuiDesktopPaintIcons();
    for (i = 0; i < GUI_WIN_COUNT; i++) {
        PaintOne((int)gZ[i]);
    }
}

void GuiWinPresentFull(void) {
    if (HalVideoBackbufferEnabled()) {
        HalVideoPresent();
    }
}

void GuiWinLayoutAll(void) {
    int i;

    gW[GUI_WIN_SHELL].W = 520u;
    gW[GUI_WIN_SHELL].H = 300u;
    gW[GUI_WIN_ABOUT].W = 340u;
    gW[GUI_WIN_ABOUT].H = 200u;
    gW[GUI_WIN_SETTINGS].W = 360u;
    gW[GUI_WIN_SETTINGS].H = 220u;
    for (i = 0; i < GUI_WIN_COUNT; i++) {
        if (gW[i].W + 40u > gFbW) {
            gW[i].W = (gFbW > 80u) ? (gFbW - 40u) : gFbW;
        }
        if (gW[i].H + GUI_BAR_H + 40u > gFbH) {
            gW[i].H = (gFbH > GUI_BAR_H + 40u) ? (gFbH - GUI_BAR_H - 40u) : 120u;
        }
    }
    gW[GUI_WIN_SHELL].X = (gFbW > gW[GUI_WIN_SHELL].W)
                              ? ((gFbW - gW[GUI_WIN_SHELL].W) / 2u)
                              : 0;
    gW[GUI_WIN_SHELL].Y = GUI_BAR_H + 24u;
    gW[GUI_WIN_ABOUT].X = gW[GUI_WIN_SHELL].X + 80u;
    gW[GUI_WIN_ABOUT].Y = gW[GUI_WIN_SHELL].Y + 60u;
    gW[GUI_WIN_SETTINGS].X = 48u;
    gW[GUI_WIN_SETTINGS].Y = GUI_BAR_H + 48u;
    if (gW[GUI_WIN_ABOUT].X + gW[GUI_WIN_ABOUT].W > gFbW) {
        gW[GUI_WIN_ABOUT].X = 40u;
    }
    if (gW[GUI_WIN_ABOUT].Y + gW[GUI_WIN_ABOUT].H > gFbH) {
        gW[GUI_WIN_ABOUT].Y = GUI_BAR_H + 40u;
    }
    gW[GUI_WIN_SHELL].Title = MSG_WIN_SHELL;
    gW[GUI_WIN_ABOUT].Title = MSG_WIN_ABOUT;
    gW[GUI_WIN_SETTINGS].Title = MSG_WIN_SETTINGS;
    gW[GUI_WIN_SHELL].On = 1;
    gW[GUI_WIN_ABOUT].On = 1;
    gW[GUI_WIN_SETTINGS].On = 0; /* 双击设置图标再开 */
    gW[GUI_WIN_SHELL].Focus = 1;
    gW[GUI_WIN_ABOUT].Focus = 0;
    gW[GUI_WIN_SETTINGS].Focus = 0;
    gZ[0] = (UINT8)GUI_WIN_ABOUT;
    gZ[1] = (UINT8)GUI_WIN_SETTINGS;
    gZ[2] = (UINT8)GUI_WIN_SHELL;
}

int GuiWinIsOn(int Id) {
    return (Id >= 0 && Id < GUI_WIN_COUNT) ? gW[Id].On : 0;
}

int GuiWinFocused(int Id) {
    return (Id >= 0 && Id < GUI_WIN_COUNT) ? (gW[Id].On && gW[Id].Focus) : 0;
}

int GuiWinHit(INT32 X, INT32 Y) {
    int i;
    for (i = GUI_WIN_COUNT - 1; i >= 0; i--) {
        int Id = (int)gZ[i];
        if (!gW[Id].On) {
            continue;
        }
        if (X < (INT32)gW[Id].X || Y < (INT32)gW[Id].Y) {
            continue;
        }
        if (X >= (INT32)(gW[Id].X + gW[Id].W) ||
            Y >= (INT32)(gW[Id].Y + gW[Id].H)) {
            continue;
        }
        return Id;
    }
    return -1;
}

int GuiWinInClose(int Id, INT32 X, INT32 Y) {
    UINT32 Bx;
    UINT32 By;
    UINT32 Bw;
    UINT32 Bh;

    if (Id < 0 || Id >= GUI_WIN_COUNT || !gW[Id].On) {
        return 0;
    }
    GuiWinPaint_CloseBtn(gW[Id].X, gW[Id].Y, gW[Id].W, &Bx, &By, &Bw, &Bh);
    if (X < (INT32)Bx || Y < (INT32)By) {
        return 0;
    }
    if (X >= (INT32)(Bx + Bw) || Y >= (INT32)(By + Bh)) {
        return 0;
    }
    return 1;
}

int GuiWinInTitle(int Id, INT32 X, INT32 Y) {
    if (Id < 0 || Id >= GUI_WIN_COUNT || !gW[Id].On) {
        return 0;
    }
    if (X < (INT32)gW[Id].X || Y < (INT32)gW[Id].Y) {
        return 0;
    }
    if (X >= (INT32)(gW[Id].X + gW[Id].W) ||
        Y >= (INT32)(gW[Id].Y + 1u + GUI_TITLE_H)) {
        return 0;
    }
    return !GuiWinInClose(Id, X, Y);
}

void GuiWinRaise(int Id) {
    int i;
    int j;

    if (Id < 0 || Id >= GUI_WIN_COUNT || !gW[Id].On) {
        return;
    }
    for (i = 0; i < GUI_WIN_COUNT; i++) {
        if ((int)gZ[i] == Id) {
            for (j = i; j < GUI_WIN_COUNT - 1; j++) {
                gZ[j] = gZ[j + 1];
            }
            gZ[GUI_WIN_COUNT - 1] = (UINT8)Id;
            return;
        }
    }
}

void GuiWinFocus(int Id) {
    int i;

    if (Id < 0 || Id >= GUI_WIN_COUNT || !gW[Id].On) {
        return;
    }
    for (i = 0; i < GUI_WIN_COUNT; i++) {
        gW[i].Focus = (i == Id) ? 1 : 0;
    }
    GuiWinRaise(Id);
    GuiWinCompose();
    GuiWinPresentFull();
    HalPs2Poll();
}

void GuiWinUnfocusAll(void) {
    int i;
    int Changed = 0;

    for (i = 0; i < GUI_WIN_COUNT; i++) {
        if (gW[i].Focus) {
            gW[i].Focus = 0;
            Changed = 1;
        }
    }
    if (!Changed) {
        return;
    }
    GuiWinCompose();
    GuiWinPresentFull();
    HalPs2Poll();
}

void GuiWinClose(int Id) {
    if (Id < 0 || Id >= GUI_WIN_COUNT || !gW[Id].On) {
        return;
    }
    GuiWinPaint_Erase(gFbW, gFbH, gW[Id].X, gW[Id].Y, gW[Id].W, gW[Id].H);
    gW[Id].On = 0;
    gW[Id].Focus = 0;
    GuiWinCompose();
    GuiWinPresentFull();
    HalPs2Poll();
    HalSerialWriteChannel(TOY_SLOG_GUI, (Id == GUI_WIN_SHELL)
                                              ? "Gui: shell closed\n"
                                              : "Gui: about closed\n");
}

void GuiWinOpen(int Id) {
    if (Id < 0 || Id >= GUI_WIN_COUNT || gW[Id].On || gFbW == 0) {
        return;
    }
    gW[Id].On = 1;
    GuiWinFocus(Id);
    HalSerialWriteChannel(TOY_SLOG_GUI, (Id == GUI_WIN_SHELL)
                                              ? "Gui: shell opened\n"
                                              : "Gui: about opened\n");
}

void GuiWinOpenMissing(void) {
    int i;
    for (i = 0; i < GUI_WIN_COUNT; i++) {
        if (!gW[i].On) {
            GuiWinOpen(i);
        }
    }
}

void GuiWinMoveTo(int Id, INT32 X, INT32 Y, int RefreshShellText) {
    UINT32 Ox;
    UINT32 Oy;
    UINT32 Ow;
    UINT32 Oh;

    if (Id < 0 || Id >= GUI_WIN_COUNT || !gW[Id].On) {
        return;
    }
    ClampPos(Id, &X, &Y);
    if ((UINT32)X == gW[Id].X && (UINT32)Y == gW[Id].Y) {
        return;
    }
    Ox = gW[Id].X;
    Oy = gW[Id].Y;
    Ow = gW[Id].W;
    Oh = gW[Id].H;
    GuiWinPaint_Erase(gFbW, gFbH, Ox, Oy, Ow, Oh);
    gW[Id].X = (UINT32)X;
    gW[Id].Y = (UINT32)Y;
    GuiWinCompose();
    GuiWinPaint_PresentMove(gFbW, gFbH, Ox, Oy, Ow, Oh, gW[Id].X, gW[Id].Y,
                            gW[Id].W, gW[Id].H);
    HalPs2Poll();
    if (RefreshShellText && Id == GUI_WIN_SHELL) {
        ConsoleRefreshBanner();
    }
}

int GuiWinGetPos(int Id, INT32 *X, INT32 *Y) {
    if (Id < 0 || Id >= GUI_WIN_COUNT || !gW[Id].On) {
        return -1;
    }
    if (X) {
        *X = (INT32)gW[Id].X;
    }
    if (Y) {
        *Y = (INT32)gW[Id].Y;
    }
    return 0;
}

int GuiWinShellClientRect(UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H) {
    if (!gW[GUI_WIN_SHELL].On) {
        return -1;
    }
    if (X) {
        *X = gW[GUI_WIN_SHELL].X + 1u;
    }
    if (Y) {
        *Y = gW[GUI_WIN_SHELL].Y + 1u + GUI_TITLE_H;
    }
    if (W) {
        *W = gW[GUI_WIN_SHELL].W - 2u;
    }
    if (H) {
        *H = (gW[GUI_WIN_SHELL].H > GUI_TITLE_H + 2u)
                 ? (gW[GUI_WIN_SHELL].H - 2u - GUI_TITLE_H)
                 : 0;
    }
    return 0;
}
