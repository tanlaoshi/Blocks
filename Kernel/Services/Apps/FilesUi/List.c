/*
 * List.c — 文件列表绘制（PR-UI-layout-apps：UiLayout 三分栏）
 *
 * PaintList 编排；侧栏 / 行表 / 预览见 companion。
 * 核心：FilesUi.c
 */
#include "FilesUiPrivate.h"
#include "Desktop.h"

void FilesMenuRegister(void) {
    DesktopMenuRegister(DESKTOP_ACTION_FILES, MSG_ICON_FILES, "Files", 2, 1);
}

static void PaintOneFileRow(int Idx, UINT32 ListX, UINT32 Pad, UINT32 Bg) {
    const FAT_DIRECTORY_ENTRY *E;
    char Line[96];
    int k = 0;
    int j;
    int Row;
    UINT32 RowY;

    if (Idx < 0 || gListLineH == 0 || gListVisible <= 0) {
        return;
    }
    if (Idx < gScroll || Idx >= gScroll + gListVisible || Idx >= gCount) {
        return;
    }
    Row = Idx - gScroll;
    RowY = gListTop + (UINT32)Row * gListLineH;
    E = &gEnts[Idx];
    if (E->Attr & FAT_ATTR_DIR) {
        Line[k++] = '[';
        Line[k++] = 'D';
        Line[k++] = ']';
        Line[k++] = ' ';
    } else {
        Line[k++] = ' ';
        Line[k++] = ' ';
        Line[k++] = ' ';
        Line[k++] = ' ';
    }
    for (j = 0; E->Name[j] && k < (int)sizeof(Line) - 1; j++) {
        Line[k++] = E->Name[j];
    }
    Line[k] = 0;
    /* 非选中/悬停时 UiDrawListRow 不铺底，须先擦掉旧高亮 */
    GraphicsFillRectangle(ListX + Pad, RowY, gListRowW, gListLineH, Bg);
    UiDrawListRow(ListX + Pad, RowY, gListRowW, gListLineH, Line,
                  Idx == gSelected, Idx == gHoverIdx);
}

void PaintList(void) {
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Bg;
    UINT32 LineH;
    UINT32 SideW;
    UINT32 ListW;
    UINT32 DetailW;
    UINT32 Cx;
    UINT32 Cw;

    if (!GuiFocusClient(&X, &Y, &W, &H, &Bg)) {
        return;
    }
    GuiFrameBufferBegin();
    GraphicsFillRectangle(X, Y, W, H, Bg);
    HalVideoSetClipRegion(X, Y, W, H, Bg);

    LineH = UiLayoutRowH();
    UiLayoutTriple(W, &SideW, &ListW, &DetailW);
    gSideW = SideW;
    gPrevW = DetailW;
    gContentX = X + SideW;
    gContentW = W - SideW;
    Cx = gContentX;
    Cw = gContentW;

    PaintListDrawSide(X, Y, H, LineH, SideW);
    PaintListDrawHeader(Cx, Y, LineH, Cw);
    PaintListDrawRows(Cx, ListW, Y, H, LineH);
    PaintListDrawPreview(Cx, Y, H, LineH, Cw);

    HalVideoClearClip();
    GuiBackupFocusWindow();
    GuiFrameBufferEnd();
}

static void SyncOneFileRow(int Idx, UINT32 ListX, UINT32 Pad) {
    int Row;
    UINT32 RowY;

    if (Idx < 0 || gListLineH == 0 || gListVisible <= 0) {
        return;
    }
    if (Idx < gScroll || Idx >= gScroll + gListVisible || Idx >= gCount) {
        return;
    }
    Row = Idx - gScroll;
    RowY = gListTop + (UINT32)Row * gListLineH;
    /* 只同步脏行：整客户区 SyncRect 逐像素 → 窗内鼠 PPT */
    GuiBackupSyncRect(ListX + Pad, RowY, gListRowW, gListLineH);
}

void PaintListHoverDelta(int OldHover, int NewHover, int OldSide, int NewSide) {
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Bg;
    UINT32 Pad;
    UINT32 ListX;
    UINT32 LineH;

    if (!GuiFocusClient(&X, &Y, &W, &H, &Bg)) {
        return;
    }
    /* 布局未建立（首悬停前）→ 整表 */
    if (gListLineH == 0 || gSideW == 0) {
        PaintList();
        return;
    }
    Pad = UI_LAYOUT_PAD;
    ListX = gContentX;
    LineH = gSideLineH ? gSideLineH : UiLayoutRowH();

    GuiFrameBufferBegin();
    HalVideoSetClipRegion(X, Y, W, H, Bg);
    if (OldSide != NewSide) {
        PaintListDrawSide(X, Y, H, LineH, gSideW);
        GuiBackupSyncRect(X, Y, gSideW, H);
    }
    if (OldHover != NewHover) {
        PaintOneFileRow(OldHover, ListX, Pad, Bg);
        PaintOneFileRow(NewHover, ListX, Pad, Bg);
        SyncOneFileRow(OldHover, ListX, Pad);
        SyncOneFileRow(NewHover, ListX, Pad);
    }
    HalVideoClearClip();
    GuiFrameBufferEnd();
}
