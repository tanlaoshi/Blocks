/*
 * GuiFiles.c — K35 列表/点 ELF；K45 选中 + New/Del
 */
#include "GuiFiles.h"
#include "FatFile.h"
#include "Font.h"
#include "Locale.h"
#include "Process.h"
#include "Theme.h"
#include "HalSerial.h"
#include "HalVideo.h"
#include "SerialConfig.h"

#define FILES_MAX   24u
#define LINE_H      22u
#define PAD_X       12u
#define PAD_Y       12u
#define BTN_H       28u
#define BTN_W       64u
#define BTN_PAD     4u   /* 命中区外扩，减轻指针 tip 与观感偏差 */

static FAT_DIR_ENT gEnts[FILES_MAX];
static UINT32 gCount;
static UINT32 gCx;
static UINT32 gCy;
static UINT32 gCw;
static UINT32 gCh;
static UINT32 gRows;
static INT32 gSel = -1;

static int NameIsElf(const char *Name) {
    int N = 0;
    while (Name[N]) {
        N++;
    }
    if (N < 4) {
        return 0;
    }
    if (Name[N - 4] != '.') {
        return 0;
    }
    {
        char A = Name[N - 3];
        char B = Name[N - 2];
        char C = Name[N - 1];
        if (A >= 'a' && A <= 'z') {
            A = (char)(A - 32);
        }
        if (B >= 'a' && B <= 'z') {
            B = (char)(B - 32);
        }
        if (C >= 'a' && C <= 'z') {
            C = (char)(C - 32);
        }
        return (A == 'E' && B == 'L' && C == 'F');
    }
}

void GuiFilesRefresh(void) {
    gCount = 0;
    gSel = -1;
    if (FatDirListRoot(gEnts, FILES_MAX, &gCount) != 0) {
        gCount = 0;
        HalSerialWriteChannel(SLOG_GUI, "Gui: files ls fail\n");
    }
}

/* New/Del 放顶栏右侧，靠近提示行，避免贴底难点 */
static void BtnBox(UINT32 Which, UINT32 *X0, UINT32 *Y0, UINT32 *X1, UINT32 *Y1) {
    UINT32 Right = gCx + gCw - PAD_X;
    UINT32 X = Right - BTN_W - (1u - Which) * (BTN_W + 8u);
    UINT32 Y = gCy + 2u;
    *X0 = X;
    *Y0 = Y;
    *X1 = X + BTN_W;
    *Y1 = Y + BTN_H;
}

void GuiFilesPaintClient(UINT32 Cx, UINT32 Cy, UINT32 Cw, UINT32 Ch) {
    UINT32 i;
    UINT32 Y;
    UINT32 MaxRows;
    UINT32 Bx0, By0, Bx1, By1;

    gCx = Cx;
    gCy = Cy;
    gCw = Cw;
    gCh = Ch;
    if (Cw < 80u || Ch < 80u) {
        return;
    }
    if (gCount == 0) {
        GuiFilesRefresh();
    }
    FontDrawStringAt(Cx + PAD_X, Cy + 8u, LocStr(MSG_FILES_HINT),
                     ThemeWindowTitleText());
    /* 顶栏占 BTN_H，列表从顶栏下开始 */
    MaxRows = (Ch > BTN_H + PAD_Y + 8u)
                  ? ((Ch - BTN_H - PAD_Y - 8u) / LINE_H)
                  : 0;
    if (MaxRows > FILES_MAX) {
        MaxRows = FILES_MAX;
    }
    gRows = (gCount < MaxRows) ? gCount : MaxRows;
    Y = Cy + BTN_H + 6u;
    for (i = 0; i < gRows; i++) {
        char Line[20];
        int P = 0;
        UINT32 Ink = ThemeWindowTitleText();
        if ((INT32)i == gSel) {
            HalVideoFillRect(Cx + 4u, Y - 1u, Cw > 8u ? Cw - 8u : Cw, LINE_H,
                             ThemeWindowTitleBar());
            Ink = ThemeWindowTitleText();
        }
        Line[P++] = gEnts[i].IsDir ? 'd' : '-';
        Line[P++] = ' ';
        {
            int k;
            for (k = 0; gEnts[i].Name[k] && P < 18; k++) {
                Line[P++] = gEnts[i].Name[k];
            }
        }
        Line[P] = 0;
        FontDrawStringAt(Cx + PAD_X, Y, Line, Ink);
        Y += LINE_H;
    }
    BtnBox(0, &Bx0, &By0, &Bx1, &By1);
    HalVideoFillRect(Bx0, By0, BTN_W, BTN_H, ThemeWindowTitleBar());
    FontDrawStringAt(Bx0 + 10u, By0 + 4u, "New", ThemeWindowTitleText());
    BtnBox(1, &Bx0, &By0, &Bx1, &By1);
    HalVideoFillRect(Bx0, By0, BTN_W, BTN_H, ThemeWindowTitleBar());
    FontDrawStringAt(Bx0 + 12u, By0 + 4u, "Del", ThemeWindowTitleText());
}

static int HitBtn(INT32 X, INT32 Y, UINT32 Which) {
    UINT32 X0, Y0, X1, Y1;
    BtnBox(Which, &X0, &Y0, &X1, &Y1);
    return (X >= (INT32)X0 - (INT32)BTN_PAD &&
            X < (INT32)X1 + (INT32)BTN_PAD &&
            Y >= (INT32)Y0 - (INT32)BTN_PAD &&
            Y < (INT32)Y1 + (INT32)BTN_PAD);
}

int GuiFilesClick(INT32 X, INT32 Y) {
    UINT32 Row;
    UINT32 Top;

    if (gCw < 80u) {
        return 0;
    }
    if (HitBtn(X, Y, 0)) {
        if (FatMkdirPath("NEW") != 0) {
            HalSerialWriteChannel(SLOG_GUI, "Gui: files new fail\n");
        } else {
            HalSerialWriteChannel(SLOG_GUI, "Gui: files new ok\n");
            GuiFilesRefresh();
        }
        return 1;
    }
    if (HitBtn(X, Y, 1)) {
        if (gSel < 0 || (UINT32)gSel >= gCount) {
            HalSerialWriteChannel(SLOG_GUI, "Gui: files no sel\n");
            return 1;
        }
        if (FatRmPath(gEnts[gSel].Name) != 0) {
            HalSerialWriteChannel(SLOG_GUI, "Gui: files del fail\n");
        } else {
            HalSerialWriteChannel(SLOG_GUI, "Gui: files del ok\n");
            GuiFilesRefresh();
        }
        return 1;
    }
    if (gRows == 0) {
        return 0;
    }
    if (X < (INT32)(gCx + 4u) || X >= (INT32)(gCx + gCw)) {
        return 0;
    }
    Top = gCy + BTN_H + 6u;
    if (Y < (INT32)Top) {
        return 0;
    }
    Row = (UINT32)(Y - (INT32)Top) / LINE_H;
    if (Row >= gRows) {
        return 0;
    }
    if ((INT32)Row == gSel && !gEnts[Row].IsDir && NameIsElf(gEnts[Row].Name)) {
        HalSerialWriteChannel(SLOG_GUI, "Gui: files exec\n");
        (void)ProcessExecPath(gEnts[Row].Name);
        return 1;
    }
    gSel = (INT32)Row;
    return 1;
}
