/*
 * SettingsUiPaint.c — PR-UI-layout-set：令牌三分栏绘制
 * 核心：SettingsUi.c
 */
#include "SettingsUiPrivate.h"

/* DrawDetail → PaintDetail.c（PR-NORM-16）。 */


static void FormatListRow(int Idx, char *Row, int RowMax) {
    char Label[48];
    int j;
    int Mark;

    if (!Row || RowMax < 4) {
        return;
    }
    ItemLabel(Idx, Label, (int)sizeof(Label));
    Mark = (Idx == CurrentItemIndex());
    Row[0] = Mark ? '*' : ' ';
    Row[1] = ' ';
    for (j = 0; Label[j] && j < RowMax - 3; j++) {
        Row[j + 2] = Label[j];
    }
    Row[j + 2] = 0;
}

static void PaintSideRow(int i, UINT32 Cx, UINT32 Pad, UINT32 RowW) {
    UINT32 Y;

    if (i < 0 || i >= SETTINGS_CAT_COUNT || gSetSideLineH == 0) {
        return;
    }
    Y = gSetSideRow0 + (UINT32)i * gSetSideLineH;
    GraphicsFillRectangle(Cx + Pad, Y, RowW, gSetSideLineH, ThemePanelSideBackground());
    UiDrawListRow(Cx + Pad, Y, RowW, gSetSideLineH,
                  CatLabel((SETTINGS_CAT)i),
                  i == (int)gCat,
                  gSetHoverKind == 0 && gSetHoverIdx == i);
}

static void PaintListRow(int Idx, UINT32 ListX, UINT32 Pad, UINT32 Bg) {
    UINT32 RowY;
    int Vis;
    char Row[56];
    int Sel;
    int Hov;

    if (Idx < 0 || gSetListLineH == 0) {
        return;
    }
    Vis = Idx - gItemScroll;
    if (Vis < 0 || Vis >= gSetListVisible) {
        return;
    }
    RowY = gSetListTop + (UINT32)Vis * gSetListLineH;
    GraphicsFillRectangle(ListX + Pad, RowY, gSetListRowW, gSetListLineH, Bg);
    FormatListRow(Idx, Row, (int)sizeof(Row));
    Sel = (Idx == gItemSel);
    Hov = (gSetHoverKind == 1 && gSetHoverIdx == Idx);
    UiDrawListRow(ListX + Pad, RowY, gSetListRowW, gSetListLineH, Row, Sel, Hov);
}

void PaintHoverDelta(int OldKind, int OldIdx, int NewKind, int NewIdx) {
    UINT32 Cx, Cy, Cw, Ch, Bg;
    UINT32 Pad;
    UINT32 RowW;

    if (GuiFocusKind() != GUI_WIN_SETTINGS) {
        return;
    }
    if (!GuiFocusClient(&Cx, &Cy, &Cw, &Ch, &Bg)) {
        return;
    }
    Pad = UI_LAYOUT_PAD;
    RowW = gSetSideW > Pad * 2u ? gSetSideW - Pad * 2u : gSetSideW;

    GuiFrameBufferBegin();
    HalVideoSetClipRegion(Cx, Cy, Cw, Ch, Bg);
    if (OldKind == 0 && OldIdx >= 0) {
        PaintSideRow(OldIdx, Cx, Pad, RowW);
        if (gSetSideLineH != 0) {
            GuiBackupSyncRect(Cx + Pad,
                              gSetSideRow0 + (UINT32)OldIdx * gSetSideLineH,
                              RowW, gSetSideLineH);
        }
    }
    if (NewKind == 0 && NewIdx >= 0 && (NewKind != OldKind || NewIdx != OldIdx)) {
        PaintSideRow(NewIdx, Cx, Pad, RowW);
        if (gSetSideLineH != 0) {
            GuiBackupSyncRect(Cx + Pad,
                              gSetSideRow0 + (UINT32)NewIdx * gSetSideLineH,
                              RowW, gSetSideLineH);
        }
    }
    if (OldKind == 1 && OldIdx >= 0) {
        PaintListRow(OldIdx, gSetListX, Pad, Bg);
        if (gSetListLineH != 0) {
            int Vis = OldIdx - gItemScroll;
            if (Vis >= 0 && Vis < gSetListVisible) {
                GuiBackupSyncRect(gSetListX + Pad,
                                  gSetListTop + (UINT32)Vis * gSetListLineH,
                                  gSetListRowW, gSetListLineH);
            }
        }
    }
    if (NewKind == 1 && NewIdx >= 0 && (NewKind != OldKind || NewIdx != OldIdx)) {
        PaintListRow(NewIdx, gSetListX, Pad, Bg);
        if (gSetListLineH != 0) {
            int Vis = NewIdx - gItemScroll;
            if (Vis >= 0 && Vis < gSetListVisible) {
                GuiBackupSyncRect(gSetListX + Pad,
                                  gSetListTop + (UINT32)Vis * gSetListLineH,
                                  gSetListRowW, gSetListLineH);
            }
        }
    }
    HalVideoClearClip();
    GuiFrameBufferEnd();
}

void PaintMenu(void) {
    UINT32 Cx, Cy, Cw, Ch, Bg;
    UINT32 LineH;
    UINT32 Pad;
    UINT32 SideW;
    UINT32 ListW;
    UINT32 DetailW;
    UINT32 ContentX;
    int i;
    int N;
    UINT32 RowY;
    UINT32 RowW;
    UINT32 TitleY;

    if (GuiFocusKind() != GUI_WIN_SETTINGS) {
        if (!FocusSettingsWindow()) {
            return;
        }
    }
    if (!GuiFocusClient(&Cx, &Cy, &Cw, &Ch, &Bg)) {
        return;
    }

    HitClear();
    GuiFrameBufferBegin();
    GraphicsFillRectangle(Cx, Cy, Cw, Ch, Bg);
    HalVideoSetClipRegion(Cx, Cy, Cw, Ch, Bg);

    LineH = UiLayoutRowH();
    Pad = UI_LAYOUT_PAD;
    UiLayoutTriple(Cw, &SideW, &ListW, &DetailW);
    gSetSideW = SideW;
    gSetPrevW = DetailW;
    TitleY = Cy + Pad;
    ContentX = Cx + SideW;

    if (SideW > 0) {
        gSetSideX = Cx;
        gSetSideY = Cy;
        gSetSideLineH = LineH;
        gSetSideRow0 = TitleY + LineH + UI_LAYOUT_GAP;
        RowW = SideW > Pad * 2u ? SideW - Pad * 2u : SideW;
        GraphicsFillRectangle(Cx, Cy, SideW, Ch, ThemePanelSideBackground());
        if (SideW > UI_LAYOUT_SEP_W) {
            GraphicsFillRectangle(Cx + SideW - UI_LAYOUT_SEP_W, Cy, UI_LAYOUT_SEP_W, Ch,
                             ThemePanelSeparator());
        }
        HalVideoDrawStringAt(Cx + Pad, TitleY, LocaleString(MESSAGE_SET_TITLE), ThemeText());
        for (i = 0; i < SETTINGS_CAT_COUNT; i++) {
            UiDrawListRow(Cx + Pad, gSetSideRow0 + (UINT32)i * LineH, RowW, LineH,
                          CatLabel((SETTINGS_CAT)i),
                          i == (int)gCat,
                          gSetHoverKind == 0 && gSetHoverIdx == i);
            HitAdd(Cx + Pad, gSetSideRow0 + (UINT32)i * LineH, RowW, LineH, 0, i);
        }
    }

    gSetPrevX = ContentX + ListW;
    gSetListX = ContentX;
    gSetListTop = TitleY + LineH + UI_LAYOUT_GAP;
    gSetListLineH = LineH;
    N = ItemCount();
    gSetListVisible = 1;
    if (Ch > Pad + LineH * 2u) {
        gSetListVisible = (int)((Ch - Pad - LineH * 2u) / LineH);
    }
    if (gSetListVisible < 1) {
        gSetListVisible = 1;
    }
    ClampItemScroll();

    gSetSbVisible = (N > gSetListVisible) ? 1 : 0;
    gSetSbW = UI_LAYOUT_SB_W;
    gSetSbH = (UINT32)gSetListVisible * LineH;
    if (gSetSbH + gSetListTop > Cy + Ch) {
        gSetSbH = (Cy + Ch > gSetListTop) ? (Cy + Ch - gSetListTop) : 0;
    }
    gSetSbX = (ListW > UI_LAYOUT_SB_W + Pad)
                  ? (ContentX + ListW - UI_LAYOUT_SB_W - UI_LAYOUT_GAP)
                  : (ContentX + Pad);
    gSetSbY = gSetListTop;
    gSetListRowW = ListW > Pad * 2u ? ListW - Pad * 2u : ListW;
    if (gSetSbVisible && gSetListRowW > UI_LAYOUT_SB_W + Pad) {
        gSetListRowW -= (UI_LAYOUT_SB_W + UI_LAYOUT_GAP);
    }

    HalVideoDrawStringAt(ContentX + Pad, TitleY, CatLabel(gCat), ThemeText());

    RowY = gSetListTop;
    for (i = 0; i < gSetListVisible && gItemScroll + i < N; i++) {
        int Idx = gItemScroll + i;
        int Sel = (Idx == gItemSel);
        int Hov = (gSetHoverKind == 1 && gSetHoverIdx == Idx);
        char Row[56];

        FormatListRow(Idx, Row, (int)sizeof(Row));
        UiDrawListRow(ContentX + Pad, RowY, gSetListRowW, LineH, Row, Sel, Hov);
        HitAdd(ContentX + Pad, RowY, gSetListRowW, LineH, 1, Idx);
        RowY += LineH;
    }
    if (gSetSbVisible && gSetSbH > 0) {
        UiDrawScrollBar(gSetSbX, gSetSbY, gSetSbW, gSetSbH, gItemScroll, gSetListVisible, N);
    }

    if (DetailW > 0) {
        DrawDetail(gSetPrevX, Cy, DetailW, Ch);
    }

    GuiBackupSyncRect(Cx, Cy, Cw, Ch);
    HalVideoClearClip();
    GuiFrameBufferEnd();
}
