/*
 * Files.c — 文件窗：根目录列表、选中、New/Del、点 ELF 执行
 *
 * 【初学者】
 * - 分层：Core/Gui；由 Window 客户区回调 Paint/Click
 * - 对外：FilesRefresh / FilesPaintClient / FilesClick
 * - 不做：子目录浏览、LFN 显示（根 8.3 列表）
 */
#include "Files.h"
#include "FatFile.h"
#include "Font.h"
#include "Locale.h"
#include "Process.h"
#include "Theme.h"
#include "HalSerial.h"
#include "HalVideo.h"
#include "SerialConfig.h"

#define FILES_MAX            24u
#define LINE_HEIGHT          22u
#define PAD_X                12u
#define PAD_Y                12u
#define BUTTON_HEIGHT        28u
#define BUTTON_WIDTH         64u
#define BUTTON_HIT_PAD       4u /* 命中区外扩，减轻指针 tip 与观感偏差 */

static FAT_DIR_ENT gEntries[FILES_MAX];
static UINT32 gCount;
static UINT32 gClientX;
static UINT32 gClientY;
static UINT32 gClientWidth;
static UINT32 gClientHeight;
static UINT32 gVisibleRows;
static INT32 gSelected = -1;

/*
 * NameIsElf — 文件名是否以 .ELF/.elf 结尾
 *
 * 谁调用：仅 FilesClick（双击语义：再点已选 ELF 则 exec）。
 * 返回：1 是；0 否
 */
static int NameIsElf(const char *Name) {
    int Length = 0;
    while (Name[Length]) {
        Length++;
    }
    if (Length < 4) {
        return 0;
    }
    if (Name[Length - 4] != '.') {
        return 0;
    }
    {
        char A = Name[Length - 3];
        char B = Name[Length - 2];
        char C = Name[Length - 1];
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

/*
 * FilesRefresh — 重读根目录到缓存并清空选中
 *
 * 谁调用：FilesPaintClient（空列表时）；FilesClick New/Del 成功后。
 * 前后文：FatDirectoryListRoot；失败则 Count=0 打日志。
 */
void FilesRefresh(void) {
    gCount = 0;
    gSelected = -1;
    if (FatDirectoryListRoot(gEntries, FILES_MAX, &gCount) != 0) {
        gCount = 0;
        HalSerialWriteChannel(SLOG_GUI, "Gui: files ls fail\n");
    }
}

/*
 * ButtonBox — New/Del 按钮矩形（Which：0=New，1=Del）
 *
 * 做什么：顶栏右侧排布；仅本文件 Paint/Hit 用。
 * 谁调用：FilesPaintClient、ButtonHit。
 */
static void ButtonBox(UINT32 Which, UINT32 *X0, UINT32 *Y0, UINT32 *X1,
                      UINT32 *Y1) {
    UINT32 Right = gClientX + gClientWidth - PAD_X;
    UINT32 X = Right - BUTTON_WIDTH - (1u - Which) * (BUTTON_WIDTH + 8u);
    UINT32 Y = gClientY + 2u;
    *X0 = X;
    *Y0 = Y;
    *X1 = X + BUTTON_WIDTH;
    *Y1 = Y + BUTTON_HEIGHT;
}

/*
 * FilesPaintClient — 画提示、列表行、New/Del
 *
 * 谁调用：Window 客户区绘制（Files 窗）。
 * 前后文：前 — 窗几何；后 — Present（由 Window 路径完成）。
 */
void FilesPaintClient(UINT32 ClientX, UINT32 ClientY, UINT32 ClientWidth,
                      UINT32 ClientHeight) {
    UINT32 i;
    UINT32 Y;
    UINT32 MaxRows;
    UINT32 BoxX0, BoxY0, BoxX1, BoxY1;

    gClientX = ClientX;
    gClientY = ClientY;
    gClientWidth = ClientWidth;
    gClientHeight = ClientHeight;
    if (ClientWidth < 80u || ClientHeight < 80u) {
        return;
    }
    if (gCount == 0) {
        FilesRefresh();
    }
    FontDrawStringAt(ClientX + PAD_X, ClientY + 8u, LocStr(MSG_FILES_HINT),
                     ThemeWindowTitleText());
    MaxRows = (ClientHeight > BUTTON_HEIGHT + PAD_Y + 8u)
                  ? ((ClientHeight - BUTTON_HEIGHT - PAD_Y - 8u) / LINE_HEIGHT)
                  : 0;
    if (MaxRows > FILES_MAX) {
        MaxRows = FILES_MAX;
    }
    gVisibleRows = (gCount < MaxRows) ? gCount : MaxRows;
    Y = ClientY + BUTTON_HEIGHT + 6u;
    for (i = 0; i < gVisibleRows; i++) {
        char Line[20];
        int Pos = 0;
        UINT32 Ink = ThemeWindowTitleText();
        if ((INT32)i == gSelected) {
            HalVideoFillRect(ClientX + 4u, Y - 1u,
                             ClientWidth > 8u ? ClientWidth - 8u : ClientWidth,
                             LINE_HEIGHT, ThemeWindowTitleBar());
            Ink = ThemeWindowTitleText();
        }
        Line[Pos++] = gEntries[i].IsDir ? 'd' : '-';
        Line[Pos++] = ' ';
        {
            int k;
            for (k = 0; gEntries[i].Name[k] && Pos < 18; k++) {
                Line[Pos++] = gEntries[i].Name[k];
            }
        }
        Line[Pos] = 0;
        FontDrawStringAt(ClientX + PAD_X, Y, Line, Ink);
        Y += LINE_HEIGHT;
    }
    ButtonBox(0, &BoxX0, &BoxY0, &BoxX1, &BoxY1);
    HalVideoFillRect(BoxX0, BoxY0, BUTTON_WIDTH, BUTTON_HEIGHT,
                     ThemeWindowTitleBar());
    FontDrawStringAt(BoxX0 + 10u, BoxY0 + 4u, "New", ThemeWindowTitleText());
    ButtonBox(1, &BoxX0, &BoxY0, &BoxX1, &BoxY1);
    HalVideoFillRect(BoxX0, BoxY0, BUTTON_WIDTH, BUTTON_HEIGHT,
                     ThemeWindowTitleBar());
    FontDrawStringAt(BoxX0 + 12u, BoxY0 + 4u, "Del", ThemeWindowTitleText());
}

/*
 * ButtonHit — 点是否落在 New/Del 命中区（含外扩）
 *
 * 谁调用：仅 FilesClick。
 * 返回：1 命中；0 未命中
 */
static int ButtonHit(INT32 X, INT32 Y, UINT32 Which) {
    UINT32 X0, Y0, X1, Y1;
    ButtonBox(Which, &X0, &Y0, &X1, &Y1);
    return (X >= (INT32)X0 - (INT32)BUTTON_HIT_PAD &&
            X < (INT32)X1 + (INT32)BUTTON_HIT_PAD &&
            Y >= (INT32)Y0 - (INT32)BUTTON_HIT_PAD &&
            Y < (INT32)Y1 + (INT32)BUTTON_HIT_PAD);
}

/*
 * FilesClick — 处理 Files 窗内点击
 *
 * 做什么：New→FatMakeDirectory；Del→FatDeleteFile；列表选中；再点 ELF→exec。
 * 谁调用：GuiPoll / Pointer 在 GUI_WIN_FILES 命中时。
 * 返回：1 已消费点击需重画；0 未处理
 */
int FilesClick(INT32 X, INT32 Y) {
    UINT32 Row;
    UINT32 Top;

    if (gClientWidth < 80u) {
        return 0;
    }
    if (ButtonHit(X, Y, 0)) {
        if (FatMakeDirectory("NEW") != 0) {
            HalSerialWriteChannel(SLOG_GUI, "Gui: files new fail\n");
        } else {
            HalSerialWriteChannel(SLOG_GUI, "Gui: files new ok\n");
            FilesRefresh();
        }
        return 1;
    }
    if (ButtonHit(X, Y, 1)) {
        if (gSelected < 0 || (UINT32)gSelected >= gCount) {
            HalSerialWriteChannel(SLOG_GUI, "Gui: files no sel\n");
            return 1;
        }
        if (FatDeleteFile(gEntries[gSelected].Name) != 0) {
            HalSerialWriteChannel(SLOG_GUI, "Gui: files del fail\n");
        } else {
            HalSerialWriteChannel(SLOG_GUI, "Gui: files del ok\n");
            FilesRefresh();
        }
        return 1;
    }
    if (gVisibleRows == 0) {
        return 0;
    }
    if (X < (INT32)(gClientX + 4u) || X >= (INT32)(gClientX + gClientWidth)) {
        return 0;
    }
    Top = gClientY + BUTTON_HEIGHT + 6u;
    if (Y < (INT32)Top) {
        return 0;
    }
    Row = (UINT32)(Y - (INT32)Top) / LINE_HEIGHT;
    if (Row >= gVisibleRows) {
        return 0;
    }
    if ((INT32)Row == gSelected && !gEntries[Row].IsDir &&
        NameIsElf(gEntries[Row].Name)) {
        HalSerialWriteChannel(SLOG_GUI, "Gui: files exec\n");
        (void)ProcessExecPath(gEntries[Row].Name);
        return 1;
    }
    gSelected = (INT32)Row;
    return 1;
}
