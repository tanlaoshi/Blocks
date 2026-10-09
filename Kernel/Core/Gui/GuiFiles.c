/*
 * GuiFiles.c — K35：根目录列表；点 .ELF → ProcessExecPath
 */
#include "GuiFiles.h"
#include "FatFile.h"
#include "Font.h"
#include "Locale.h"
#include "Process.h"
#include "Theme.h"
#include "HalSerial.h"
#include "ToySerialConfig.h"

#define FILES_MAX   24u
#define LINE_H      20u
#define PAD_X       12u
#define PAD_Y       12u

static FAT_DIR_ENT gEnts[FILES_MAX];
static UINT32 gCount;
static UINT32 gCx;
static UINT32 gCy;
static UINT32 gCw;
static UINT32 gCh;
static UINT32 gRows;

static int NameIsElf(const char *Name) {
    int N = 0;
    while (Name[N]) {
        N++;
    }
    if (N < 4) {
        return 0;
    }
    /* 后缀 .ELF / .elf */
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
    if (FatDirListRoot(gEnts, FILES_MAX, &gCount) != 0) {
        gCount = 0;
        HalSerialWriteChannel(TOY_SLOG_GUI, "Gui: files ls fail\n");
    }
}

void GuiFilesPaintClient(UINT32 Cx, UINT32 Cy, UINT32 Cw, UINT32 Ch) {
    UINT32 i;
    UINT32 Y;
    UINT32 MaxRows;

    gCx = Cx;
    gCy = Cy;
    gCw = Cw;
    gCh = Ch;
    if (Cw < 40u || Ch < 40u) {
        return;
    }
    if (gCount == 0) {
        GuiFilesRefresh();
    }
    FontDrawStringAt(Cx + PAD_X, Cy + 4u, LocStr(MSG_FILES_HINT),
                     ThemeWindowTitleText());
    MaxRows = (Ch > PAD_Y + LINE_H) ? ((Ch - PAD_Y - LINE_H) / LINE_H) : 0;
    if (MaxRows > FILES_MAX) {
        MaxRows = FILES_MAX;
    }
    gRows = (gCount < MaxRows) ? gCount : MaxRows;
    Y = Cy + PAD_Y + LINE_H;
    for (i = 0; i < gRows; i++) {
        char Line[20];
        int P = 0;
        Line[P++] = gEnts[i].IsDir ? 'd' : '-';
        Line[P++] = ' ';
        {
            int k;
            for (k = 0; gEnts[i].Name[k] && P < 18; k++) {
                Line[P++] = gEnts[i].Name[k];
            }
        }
        Line[P] = 0;
        FontDrawStringAt(Cx + PAD_X, Y, Line, ThemeWindowTitleText());
        Y += LINE_H;
    }
}

int GuiFilesClick(INT32 X, INT32 Y) {
    UINT32 Row;
    UINT32 Top;

    if (gRows == 0 || gCw < 40u) {
        return 0;
    }
    if (X < (INT32)(gCx + PAD_X) || X >= (INT32)(gCx + gCw)) {
        return 0;
    }
    Top = gCy + PAD_Y + LINE_H;
    if (Y < (INT32)Top) {
        return 0;
    }
    Row = (UINT32)(Y - (INT32)Top) / LINE_H;
    if (Row >= gRows) {
        return 0;
    }
    if (gEnts[Row].IsDir) {
        return 0;
    }
    if (!NameIsElf(gEnts[Row].Name)) {
        HalSerialWriteChannel(TOY_SLOG_GUI, "Gui: files not elf\n");
        return 0;
    }
    HalSerialWriteChannel(TOY_SLOG_GUI, "Gui: files exec\n");
    (void)ProcessExecPath(gEnts[Row].Name);
    return 1;
}
