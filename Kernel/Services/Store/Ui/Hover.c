/*
 * Hover.c — 商店悬停脏行（对表 Files PaintListHoverDelta / Settings）
 *
 * 旧路径悬停 → StoreUiRepaint 整客户区 Fill + PresentFlush，
 * 窗内空拖必卡鼠。此处只重画旧/新侧栏行、列表行、底钮。
 */
#include "StoreUiPrivate.h"
#include "GuiPrivate.h"

static UINT32 ColumnAdvance(void) {
    UINT32 Advance = FontAdvanceX();
    return (Advance < 8u) ? 8u : Advance;
}

static void StoreHoverColumns(UINT32 *Source, UINT32 *Identifier, UINT32 *Name) {
    UINT32 Advance = ColumnAdvance();

    *Source = Advance * 6u;
    *Identifier = *Source + Advance * 8u;
    *Name = *Identifier + Advance * 12u;
}

static void DrawTextField(UINT32 X, UINT32 Y, UINT32 MaxW, const char *Text, UINT32 Fg) {
    char Buf[40];
    int i;

    if (!Text || MaxW < 4u) {
        return;
    }
    for (i = 0; Text[i] && i < (int)sizeof(Buf) - 1; i++) {
        Buf[i] = Text[i];
    }
    Buf[i] = 0;
    UiFitTextUtf8(Buf, MaxW);
    HalVideoDrawStringAt(X, Y, Buf, Fg);
}

static void PaintSideRow(int Index, UINT32 ClientX, UINT32 Pad, UINT32 RowWidth) {
    UINT32 Y;

    if (Index < 0 || Index >= STORE_CAT_COUNT || gStoreUiSideLineH == 0) {
        return;
    }
    Y = gStoreUiSideRow0 + (UINT32)Index * gStoreUiSideLineH;
    GraphicsFillRectangle(ClientX + Pad, Y, RowWidth, gStoreUiSideLineH,
                          ThemePanelSideBackground());
    UiDrawListRow(ClientX + Pad, Y, RowWidth, gStoreUiSideLineH,
                  StoreCatLabel(Index), Index == gStoreUiCat, Index == gHoverSide);
    GuiBackupSyncRect(ClientX + Pad, Y, RowWidth, gStoreUiSideLineH);
}

static void PaintListRow(int FilterIndex, UINT32 ListX, UINT32 Pad, UINT32 Background) {
    int Visible;
    int CatalogIndex;
    int Installed;
    UINT32 RowY;
    UINT32 TextY;
    UINT32 Foreground;
    UINT32 ColumnSource;
    UINT32 ColumnIdentifier;
    UINT32 ColumnName;
    STORE_ENTRY *Table;
    const char *SourceLabel;

    if (FilterIndex < 0 || gStoreUiListLineH == 0) {
        return;
    }
    Visible = FilterIndex - gStoreUiScroll;
    if (Visible < 0 || Visible >= gStoreUiListVisible || FilterIndex >= gFiltCount) {
        return;
    }
    CatalogIndex = gMap[FilterIndex];
    Table = StoreScratchTab();
    RowY = gStoreUiListTop + (UINT32)Visible * gStoreUiListLineH;
    GraphicsFillRectangle(ListX + Pad, RowY, gStoreUiListRowW, gStoreUiListLineH,
                          Background);
    UiDrawListRow(ListX + Pad, RowY, gStoreUiListRowW, gStoreUiListLineH, "",
                  FilterIndex == gSel, FilterIndex == gHoverRow);
    StoreHoverColumns(&ColumnSource, &ColumnIdentifier, &ColumnName);
    Foreground = (FilterIndex == gSel) ? ThemeTextOnAccent() : ThemeText();
    TextY = RowY + (gStoreUiListLineH > FontCellH()
                        ? (gStoreUiListLineH - FontCellH()) / 2u
                        : 0);
    Installed = CachedInstalled(CatalogIndex);
    SourceLabel = (Table[CatalogIndex].Origin == STORE_SRC_NET)
                      ? LocaleString(MESSAGE_STORE_SRC_NET)
                      : LocaleString(MESSAGE_STORE_SRC_LOCAL);
    DrawTextField(ListX + Pad, TextY,
                  ColumnSource > 4u ? ColumnSource - 4u : ColumnSource,
                  Installed ? "*" : "", Foreground);
    DrawTextField(ListX + Pad + ColumnSource, TextY,
                  (ColumnIdentifier > ColumnSource + 4u)
                      ? ColumnIdentifier - ColumnSource - 4u
                      : 0,
                  SourceLabel, Foreground);
    DrawTextField(ListX + Pad + ColumnIdentifier, TextY,
                  (ColumnName > ColumnIdentifier + 4u)
                      ? ColumnName - ColumnIdentifier - 4u
                      : 0,
                  Table[CatalogIndex].Id, Foreground);
    DrawTextField(ListX + Pad + ColumnName, TextY,
                  (gStoreUiListRowW > ColumnName + 4u)
                      ? gStoreUiListRowW - ColumnName
                      : 0,
                  Table[CatalogIndex].Title, Foreground);
    GuiBackupSyncRect(ListX + Pad, RowY, gStoreUiListRowW, gStoreUiListLineH);
}

static void PaintButtonsStrip(UINT32 ClientX, UINT32 ClientY, UINT32 ClientW,
                              UINT32 ClientH, UINT32 Background) {
    int ButtonIndex;
    UINT32 ButtonX;
    UINT32 StripY;
    UINT32 StripH;
    int ShellBusy = StoreJobShellIsBusy();
    int UiBusy = StoreJobIsBusy() && !ShellBusy;

    (void)ClientX;
    (void)ClientW;
    StripY = gBtnY;
    StripH = STORE_BTN_H;
    if (StripY + StripH > ClientY + ClientH) {
        return;
    }
    GraphicsFillRectangle(gBtnX0, StripY,
                          (UINT32)STORE_BTN_N * (gBtnW + STORE_BTN_GAP), StripH,
                          Background);
    for (ButtonIndex = 0; ButtonIndex < STORE_BTN_N; ButtonIndex++) {
        ButtonX = gBtnX0 + (UINT32)ButtonIndex * (gBtnW + STORE_BTN_GAP);
        if (ShellBusy) {
            UiDrawButtonEx(ButtonX, gBtnY, gBtnW, STORE_BTN_H,
                           StoreBtnLabel(ButtonIndex), ThemeTextMuted(),
                           ThemePanelSideBackground(), 0, 0);
        } else if (UiBusy) {
            if (ButtonIndex == 0) {
                UINT32 Face = (gHoverBtn == 0) ? ThemeControlAccent()
                                              : ThemeControlFace();
                UINT32 Foreground = (gHoverBtn == 0) ? ThemeTextOnAccent()
                                                    : ThemeText();
                UiDrawButtonEx(ButtonX, gBtnY, gBtnW, STORE_BTN_H,
                               LocaleString(MESSAGE_STORE_CANCEL), Foreground, Face,
                               gHoverBtn == 0, gPressBtn == 0);
            } else {
                UiDrawButtonEx(ButtonX, gBtnY, gBtnW, STORE_BTN_H,
                               StoreBtnLabel(ButtonIndex), ThemeTextMuted(),
                               ThemePanelSideBackground(), 0, 0);
            }
        } else {
            UINT32 Face = (gHoverBtn == ButtonIndex) ? ThemeControlAccent()
                                                    : ThemeControlFace();
            UINT32 Foreground = (gHoverBtn == ButtonIndex) ? ThemeTextOnAccent()
                                                          : ThemeText();
            UiDrawButtonEx(ButtonX, gBtnY, gBtnW, STORE_BTN_H,
                           StoreBtnLabel(ButtonIndex), Foreground, Face,
                           gHoverBtn == ButtonIndex, gPressBtn == ButtonIndex);
        }
    }
    GuiBackupSyncRect(gBtnX0, StripY,
                      (UINT32)STORE_BTN_N * (gBtnW + STORE_BTN_GAP), StripH);
}

void StorePaintStatusOnly(void) {
    UINT32 Cx, Cy, Cw, Ch, Bg;
    UINT32 LineH;
    UINT32 Pad;
    UINT32 StatusY;
    UINT32 StatusW;
    UINT32 ListW;
    char Status[96];
    int si;

    if (!StoreUiIsFocused()) {
        return;
    }
    if (!GuiFocusClient(&Cx, &Cy, &Cw, &Ch, &Bg)) {
        return;
    }
    if (gStoreUiListLineH == 0 || gListX == 0) {
        return;
    }
    LineH = UiLayoutRowH();
    Pad = UI_LAYOUT_PAD;
    StatusY = Cy + Ch - LineH - 2u;
    if (StatusY < Cy || StatusY + LineH > Cy + Ch) {
        return;
    }
    if (gStoreUiPrevW > 0 && gStoreUiPrevX > gListX) {
        ListW = gStoreUiPrevX - gListX;
    } else if (Cw > gStoreUiSideW) {
        ListW = Cw - gStoreUiSideW;
    } else {
        ListW = Cw;
    }
    StatusW = ListW > Pad * 2u ? ListW - Pad * 2u : ListW;
    for (si = 0; gStoreUiStatus[si] && si < (int)sizeof(Status) - 1; si++) {
        Status[si] = gStoreUiStatus[si];
    }
    Status[si] = 0;
    UiFitTextUtf8(Status, StatusW);

    GuiFrameBufferBegin();
    HalVideoSetClipRegion(Cx, Cy, Cw, Ch, Bg);
    GraphicsFillRectangle(gListX + Pad, StatusY, StatusW, LineH, Bg);
    if (Status[0]) {
        HalVideoDrawStringAt(gListX + Pad, StatusY, Status, ThemeTextMuted());
    }
    GuiBackupSyncRect(gListX + Pad, StatusY, StatusW, LineH);
    HalVideoClearClip();
    GuiFrameBufferEnd();
}

void StorePaintHoverDelta(int OldSide, int NewSide, int OldRow, int NewRow,
                          int OldBtn, int NewBtn) {
    UINT32 Cx, Cy, Cw, Ch, Bg;
    UINT32 Pad;
    UINT32 RowW;

    if (!StoreUiIsFocused()) {
        return;
    }
    if (!GuiFocusClient(&Cx, &Cy, &Cw, &Ch, &Bg)) {
        return;
    }
    if (gStoreUiSideLineH == 0 || gStoreUiListLineH == 0) {
        StorePaintList();
        return;
    }
    Pad = UI_LAYOUT_PAD;
    RowW = gStoreUiSideW > Pad * 2u ? gStoreUiSideW - Pad * 2u : gStoreUiSideW;

    GuiFrameBufferBegin();
    HalVideoSetClipRegion(Cx, Cy, Cw, Ch, Bg);
    if (OldSide != NewSide) {
        if (OldSide >= 0) {
            PaintSideRow(OldSide, Cx, Pad, RowW);
        }
        if (NewSide >= 0 && NewSide != OldSide) {
            PaintSideRow(NewSide, Cx, Pad, RowW);
        }
    }
    if (OldRow != NewRow) {
        if (OldRow >= 0) {
            PaintListRow(OldRow, gListX, Pad, Bg);
        }
        if (NewRow >= 0 && NewRow != OldRow) {
            PaintListRow(NewRow, gListX, Pad, Bg);
        }
    }
    if (OldBtn != NewBtn) {
        PaintButtonsStrip(Cx, Cy, Cw, Ch, Bg);
    }
    HalVideoClearClip();
    GuiFrameBufferEnd();
}
