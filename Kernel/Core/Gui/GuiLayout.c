/*
 * GuiLayout.c — 窗/栏/图标几何描述表 + FitScale + LAYOUT.CFG
 *
 * 【初学者】改窗大小先改本表或 LAYOUT.CFG，勿在 GuiWinLayoutAll 里写魔法数。
 */
#include "GuiLayout.h"
#include "GuiWin.h"
#include "FatFile.h"
#include "FileSystem.h"
#include "HalSerial.h"
#include "SerialConfig.h"

#define CFG_PATH "LAYOUT.CFG"
#define CFG_MAX  512u

static UINT32 gFbW;
static UINT32 gFbH;
static UINT32 gBarH = 32u;   /* 吃下 Terminus/CJK 18 + 垫 */
static UINT32 gTitleH = 28u;
static UINT32 gCloseW = 22u;
static UINT32 gIconX0 = 28u;
static UINT32 gIconY0 = 48u;
static UINT32 gIconTile = 40u;
static UINT32 gIconGap = 6u;
static UINT32 gIconLabelH = 18u;
static UINT32 gIconStride = 88u;
static UINT32 gStartBtnX = 6u;
static UINT32 gStartBtnW = 92u;
static UINT32 gStartBtnH = 22u;
static UINT32 gStartPadY = 4u;
static UINT32 gMenuW = 140u;
static UINT32 gMenuRow = 24u;
static UINT32 gMenuN = 3u;

/*
 * 内建描述（设计稿 1280×720）。
 * Place：CENTER=水平居中，Y=内容区偏移；XY=内容区左上；SHELL_DELTA=相对 Shell。
 */
static GUI_WIN_LAYOUT gWin[GUI_WIN_COUNT] = {
    {"shell", 520u, 300u, 280u, 160u, GUI_PLACE_CENTER, 0, 24, 1},
    {"about", 340u, 200u, 200u, 120u, GUI_PLACE_SHELL_DELTA, 80, 60, 1},
    {"settings", 360u, 220u, 240u, 140u, GUI_PLACE_XY, 48, 48, 0},
    {"files", 380u, 260u, 240u, 160u, GUI_PLACE_XY, 72, 72, 0},
};

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
        S = 400u; /* 地板，避免钮不可点 */
    }
    return S;
}

static UINT32 ScaleDesign(UINT32 DesignPx) {
    return (UINT32)(((UINT64)DesignPx * (UINT64)FitScalePermille()) / 1000u);
}

void GuiLayoutSetFb(UINT32 W, UINT32 H) {
    gFbW = W;
    gFbH = H;
}

UINT32 GuiLayoutFbW(void) {
    return gFbW;
}

UINT32 GuiLayoutFbH(void) {
    return gFbH;
}

UINT32 GuiLayoutBarH(void) {
    UINT32 H = ScaleDesign(gBarH);
    return (H < 20u) ? 20u : H;
}

UINT32 GuiLayoutTitleH(void) {
    UINT32 H = ScaleDesign(gTitleH);
    return (H < 20u) ? 20u : H;
}

UINT32 GuiLayoutCloseW(void) {
    UINT32 W = ScaleDesign(gCloseW);
    return (W < 16u) ? 16u : W;
}

UINT32 GuiLayoutContentTop(void) {
    return GuiLayoutBarH();
}

UINT32 GuiLayoutContentBottom(void) {
    UINT32 Bar = GuiLayoutBarH();
    return (gFbH > Bar) ? (gFbH - Bar) : 0;
}

UINT32 GuiLayoutPx(UINT32 DesignPx) {
    return ScaleDesign(DesignPx);
}

const GUI_WIN_LAYOUT *GuiLayoutWinDesc(int WinId) {
    if (WinId < 0 || WinId >= GUI_WIN_COUNT) {
        return 0;
    }
    return &gWin[WinId];
}

void GuiLayoutResolveWin(int WinId, UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H) {
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
    D = GuiLayoutWinDesc(WinId);
    if (D == 0) {
        *X = *Y = 0;
        *W = *H = 0;
        return;
    }
    Top = GuiLayoutContentTop();
    Bot = GuiLayoutContentBottom();
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
        /* SHELL_DELTA：先解析 shell */
        GuiLayoutResolveWin(GUI_WIN_SHELL, &Sx, &Sy, &Sw, &Sh);
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

UINT32 GuiLayoutIconTile(void) {
    UINT32 T = ScaleDesign(gIconTile);
    return (T < 24u) ? 24u : T;
}

void GuiLayoutIconSlot(int Slot, UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H) {
    UINT32 Tile = GuiLayoutIconTile();
    UINT32 Gap = ScaleDesign(gIconGap);
    UINT32 Lab = ScaleDesign(gIconLabelH);
    UINT32 Stride = ScaleDesign(gIconStride);

    if (Slot < 0) {
        Slot = 0;
    }
    *X = ScaleDesign(gIconX0) + (UINT32)Slot * Stride;
    *Y = ScaleDesign(gIconY0);
    if (*Y < GuiLayoutContentTop()) {
        *Y = GuiLayoutContentTop() + ScaleDesign(20u);
    }
    *W = Tile + ScaleDesign(24u);
    *H = Tile + Gap + Lab;
}

void GuiLayoutStartBtn(UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H) {
    UINT32 BarY = GuiLayoutContentBottom();
    *X = ScaleDesign(gStartBtnX);
    *Y = BarY + ScaleDesign(gStartPadY);
    *W = ScaleDesign(gStartBtnW);
    *H = ScaleDesign(gStartBtnH);
    if (*W < 72u) {
        *W = 72u;
    }
    if (*H < 16u) {
        *H = 16u;
    }
}

void GuiLayoutStartMenu(UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H) {
    UINT32 Bx;
    UINT32 By;
    UINT32 Bw;
    UINT32 Bh;
    UINT32 Mh;

    GuiLayoutStartBtn(&Bx, &By, &Bw, &Bh);
    Mh = gMenuN * ScaleDesign(gMenuRow) + ScaleDesign(8u);
    *X = Bx;
    *W = ScaleDesign(gMenuW);
    *H = Mh;
    *Y = (GuiLayoutContentBottom() > Mh) ? (GuiLayoutContentBottom() - Mh) : 0;
    (void)By;
    (void)Bh;
}

static int KeyEq(const char *Line, const char *Key, const char **Val) {
    UINTN i = 0;
    while (Key[i] != 0) {
        if (Line[i] != Key[i]) {
            return 0;
        }
        i++;
    }
    if (Line[i] != '=') {
        return 0;
    }
    *Val = Line + i + 1;
    return 1;
}

static int ParseU32(const char **S, UINT32 *Out) {
    UINT32 V = 0;
    int Dig = 0;
    const char *P = *S;
    while (*P >= '0' && *P <= '9') {
        V = V * 10u + (UINT32)(*P - '0');
        P++;
        Dig = 1;
        if (V > 10000u) {
            return 0;
        }
    }
    if (!Dig) {
        return 0;
    }
    *S = P;
    *Out = V;
    return 1;
}

static int ParseWxH(const char *S, UINT32 *W, UINT32 *H) {
    if (!ParseU32(&S, W) || (*S != 'x' && *S != 'X')) {
        return 0;
    }
    S++;
    if (!ParseU32(&S, H)) {
        return 0;
    }
    return 1;
}

static void ApplyWinSize(const char *Name, UINT32 W, UINT32 H) {
    int i;
    for (i = 0; i < GUI_WIN_COUNT; i++) {
        const char *N = gWin[i].Name;
        UINTN k = 0;
        int Eq = 1;
        while (Name[k] != 0 || N[k] != 0) {
            if (Name[k] != N[k]) {
                Eq = 0;
                break;
            }
            k++;
        }
        if (Eq) {
            gWin[i].DesignW = W;
            gWin[i].DesignH = H;
            return;
        }
    }
}

static void ApplyLine(const char *Line) {
    const char *Val = 0;
    UINT32 A;
    UINT32 B;

    while (*Line == ' ' || *Line == '\t') {
        Line++;
    }
    if (*Line == 0 || *Line == '#') {
        return;
    }
    if (KeyEq(Line, "bar_h", &Val) && ParseU32(&Val, &A)) {
        gBarH = A;
        return;
    }
    if (KeyEq(Line, "title_h", &Val) && ParseU32(&Val, &A)) {
        gTitleH = A;
        return;
    }
    if (KeyEq(Line, "icon_tile", &Val) && ParseU32(&Val, &A)) {
        gIconTile = A;
        return;
    }
    if (KeyEq(Line, "icon_stride", &Val) && ParseU32(&Val, &A)) {
        gIconStride = A;
        return;
    }
    if (KeyEq(Line, "shell", &Val) && ParseWxH(Val, &A, &B)) {
        ApplyWinSize("shell", A, B);
        return;
    }
    if (KeyEq(Line, "about", &Val) && ParseWxH(Val, &A, &B)) {
        ApplyWinSize("about", A, B);
        return;
    }
    if (KeyEq(Line, "settings", &Val) && ParseWxH(Val, &A, &B)) {
        ApplyWinSize("settings", A, B);
        return;
    }
    if (KeyEq(Line, "files", &Val) && ParseWxH(Val, &A, &B)) {
        ApplyWinSize("files", A, B);
        return;
    }
}

int GuiLayoutLoadCfg(void) {
    char Buf[CFG_MAX];
    UINT32 Size = 0;
    UINT32 i;
    char Line[80];
    UINTN L = 0;

    if (!FileSystemHasOsMarker()) {
        return -1;
    }
    if (FatFileReadPath(CFG_PATH, Buf, sizeof(Buf) - 1u, &Size) < 0 ||
        Size == 0) {
        HalSerialWriteChannel(SLOG_GUI, "Layout: cfg miss (builtin)\n");
        return -1;
    }
    Buf[Size] = 0;
    for (i = 0; i <= Size; i++) {
        char C = (i < Size) ? Buf[i] : '\n';
        if (C == '\n' || C == '\r' || i == Size) {
            if (L > 0) {
                Line[L] = 0;
                ApplyLine(Line);
                L = 0;
            }
            continue;
        }
        if (L + 1u < sizeof(Line)) {
            Line[L++] = C;
        }
    }
    HalSerialWriteChannel(SLOG_GUI, "Layout: cfg loaded\n");
    return 0;
}
