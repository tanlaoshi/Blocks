/*
 * PaintList.c — Store 列表绘制 / 加宽（自 Paint.c 纯搬 · PR-NORM-16）
 */
#include "StoreUiPrivate.h"
#include "GuiPrivate.h"

static void DrawButtons(void) {
    int i;
    UINT32 Bx;
    int ShellBusy = StoreJobShellIsBusy();
    int UiBusy = StoreJobIsBusy() && !ShellBusy;

    for (i = 0; i < STORE_BTN_N; i++) {
        Bx = gBtnX0 + (UINT32)i * (gBtnW + STORE_BTN_GAP);
        if (ShellBusy) {
            UiDrawButtonEx(Bx, gBtnY, gBtnW, STORE_BTN_H, StoreBtnLabel(i), ThemeTextMuted(),
                           ThemePanelSideBackground(), 0, 0);
        } else if (UiBusy) {
            if (i == 0) {
                UINT32 Face = (gHoverBtn == 0) ? ThemeControlAccent() : ThemeControlFace();
                UINT32 Fg = (gHoverBtn == 0) ? ThemeTextOnAccent() : ThemeText();
                UiDrawButtonEx(Bx, gBtnY, gBtnW, STORE_BTN_H, LocaleString(MESSAGE_STORE_CANCEL), Fg, Face,
                               gHoverBtn == 0, gPressBtn == 0);
            } else {
                UiDrawButtonEx(Bx, gBtnY, gBtnW, STORE_BTN_H, StoreBtnLabel(i), ThemeTextMuted(),
                               ThemePanelSideBackground(), 0, 0);
            }
        } else {
            UINT32 Face = (gHoverBtn == i) ? ThemeControlAccent() : ThemeControlFace();
            UINT32 Fg = (gHoverBtn == i) ? ThemeTextOnAccent() : ThemeText();
            UiDrawButtonEx(Bx, gBtnY, gBtnW, STORE_BTN_H, StoreBtnLabel(i), Fg, Face,
                           gHoverBtn == i, gPressBtn == i);
        }
    }
}

static void StoreDrawField(UINT32 X, UINT32 Y, UINT32 MaxW, const char *Text, UINT32 Fg) {
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

void StorePaintList(void) {
    UINT32 Cx, Cy, Cw, Ch, Bg;
    UINT32 LineH;
    UINT32 Pad;
    UINT32 SideW;
    UINT32 ListW;
    UINT32 DetailW;
    UINT32 ContentX;
    UINT32 RowY;
    UINT32 RowW;
    UINT32 FootH;
    UINT32 ListBottom;
    UINT32 TitleY;
    int i;
    STORE_ENTRY *Tab = StoreScratchTab();
    UINT32 ColSrc;
    UINT32 ColId;
    UINT32 ColName;
    UINT32 ColNeed;
    UINT32 TextY;
    UINT32 HeaderY;
    UINT32 OldW;

    if (!GuiFocusClient(&Cx, &Cy, &Cw, &Ch, &Bg)) {
        return;
    }
    OldW = Cw;
    StoreWidenList(&Cw);
    GuiFrameBufferBegin();
    /*
     * 加宽只改了 Win->Width。标题和客户区会重画，右边 GUI_CLIENT_PAD
     * 那一圈原先靠开窗时铺的底板，这里不补就会露出桌面。
     */
    if (Cw != OldW && gGuiWindowManager.FocusWin >= 0 &&
        gGuiWindowManager.FocusWin < MAX_WINS) {
        GUI_WINDOW *Win = &gGuiWindowManager.Windows[gGuiWindowManager.FocusWin];

        if (Win->Width > 2 && Win->Height > TITLE_HEIGHT + 1) {
            GraphicsFillRectangle(Win->X + 1, Win->Y + TITLE_HEIGHT, Win->Width - 2,
                            Win->Height - TITLE_HEIGHT - 1, Win->Background);
        }
        DrawWindowChromeAt(gGuiWindowManager.FocusWin);
    }
    GraphicsFillRectangle(Cx, Cy, Cw, Ch, Bg);
    HalVideoSetClipRegion(Cx, Cy, Cw, Ch, Bg);

    LineH = UiLayoutRowH();
    Pad = UI_LAYOUT_PAD;
    UiLayoutTriple(Cw, &SideW, &ListW, &DetailW);
    gStoreUiSideW = SideW;
    gStoreUiPrevW = DetailW;
    ContentX = Cx + SideW;
    TitleY = Cy + Pad;

    if (SideW > 0) {
        gStoreUiSideX = Cx;
        gStoreUiSideLineH = LineH;
        gStoreUiSideRow0 = TitleY + LineH + UI_LAYOUT_GAP;
        RowW = SideW > Pad * 2u ? SideW - Pad * 2u : SideW;
        GraphicsFillRectangle(Cx, Cy, SideW, Ch, ThemePanelSideBackground());
        if (SideW > UI_LAYOUT_SEP_W) {
            GraphicsFillRectangle(Cx + SideW - UI_LAYOUT_SEP_W, Cy, UI_LAYOUT_SEP_W, Ch,
                             ThemePanelSeparator());
        }
        HalVideoDrawStringAt(Cx + Pad, TitleY, LocaleString(MESSAGE_APP_STORE), ThemeText());
        for (i = 0; i < STORE_CAT_COUNT; i++) {
            UiDrawListRow(Cx + Pad, gStoreUiSideRow0 + (UINT32)i * LineH, RowW, LineH,
                          StoreCatLabel(i), i == gStoreUiCat, i == gHoverSide);
        }
    }

    gListX = ContentX;
    FootH = STORE_BTN_H + LineH + Pad + UI_LAYOUT_GAP;
    if (FootH + LineH * 4 > Ch) {
        FootH = STORE_BTN_H + Pad;
    }
    ListBottom = Cy + Ch - FootH;
    HeaderY = TitleY + LineH + UI_LAYOUT_GAP;
    gStoreUiListTop = HeaderY + LineH;
    gStoreUiListLineH = LineH;
    gStoreUiListVisible = 1;
    if (ListBottom > gStoreUiListTop + LineH) {
        gStoreUiListVisible = (int)((ListBottom - gStoreUiListTop) / LineH);
    }
    if (gStoreUiListVisible < 1) {
        gStoreUiListVisible = 1;
    }
    ClampScroll();

    gStoreUiSbVisible = (gFiltCount > gStoreUiListVisible) ? 1 : 0;
    gStoreUiSbW = STORE_SB_W;
    gStoreUiSbH = (UINT32)gStoreUiListVisible * LineH;
    if (gStoreUiSbH + gStoreUiListTop > ListBottom) {
        gStoreUiSbH = (ListBottom > gStoreUiListTop) ? (ListBottom - gStoreUiListTop) : 0;
    }
    gStoreUiSbX = (ListW > STORE_SB_W + Pad)
                      ? (ContentX + ListW - STORE_SB_W - UI_LAYOUT_GAP)
                      : (ContentX + Pad);
    gStoreUiSbY = gStoreUiListTop;
    gStoreUiListRowW = ListW > Pad * 2u ? ListW - Pad * 2u : ListW;
    if (gStoreUiSbVisible && gStoreUiListRowW > STORE_SB_W + Pad) {
        gStoreUiListRowW -= (STORE_SB_W + UI_LAYOUT_GAP);
    }

    HalVideoDrawStringAt(ContentX + Pad, TitleY, StoreCatLabel(gStoreUiCat), ThemeText());
    StoreColumns(&ColSrc, &ColId, &ColName, &ColNeed);
    TextY = HeaderY + (LineH > FontCellH() ? (LineH - FontCellH()) / 2u : 0);
    StoreDrawField(ContentX + Pad, TextY, ColSrc > 4u ? ColSrc - 4u : ColSrc,
                   LocaleString(MESSAGE_STORE_COL_MARK), ThemeTextMuted());
    StoreDrawField(ContentX + Pad + ColSrc, TextY,
                   (ColId > ColSrc + 4u) ? ColId - ColSrc - 4u : 0,
                   LocaleString(MESSAGE_STORE_COL_SRC), ThemeTextMuted());
    StoreDrawField(ContentX + Pad + ColId, TextY,
                   (ColName > ColId + 4u) ? ColName - ColId - 4u : 0,
                   LocaleString(MESSAGE_STORE_COL_ID), ThemeTextMuted());
    StoreDrawField(ContentX + Pad + ColName, TextY,
                   (gStoreUiListRowW > ColName + 4u) ? gStoreUiListRowW - ColName : 0,
                   LocaleString(MESSAGE_STORE_COL_NAME), ThemeTextMuted());
    if (gStoreUiListRowW > 0) {
        GraphicsFillRectangle(ContentX + Pad, HeaderY + LineH - 1u, gStoreUiListRowW, 1u,
                        ThemePanelSeparator());
    }

    RowY = gStoreUiListTop;
    for (i = 0; i < gStoreUiListVisible && gStoreUiScroll + i < gFiltCount; i++) {
        int Fi = gStoreUiScroll + i;
        int Ci = gMap[Fi];
        int Inst = CachedInstalled(Ci);
        UINT32 Fg;
        const char *Src;

        UiDrawListRow(ContentX + Pad, RowY, gStoreUiListRowW, LineH, "",
                      Fi == gSel, Fi == gHoverRow);
        Fg = (Fi == gSel) ? ThemeTextOnAccent() : ThemeText();
        TextY = RowY + (LineH > FontCellH() ? (LineH - FontCellH()) / 2u : 0);
        Src = (Tab[Ci].Origin == STORE_SRC_NET) ? LocaleString(MESSAGE_STORE_SRC_NET)
                                                : LocaleString(MESSAGE_STORE_SRC_LOCAL);
        StoreDrawField(ContentX + Pad, TextY, ColSrc > 4u ? ColSrc - 4u : ColSrc,
                       Inst ? "*" : "", Fg);
        StoreDrawField(ContentX + Pad + ColSrc, TextY,
                       (ColId > ColSrc + 4u) ? ColId - ColSrc - 4u : 0, Src, Fg);
        StoreDrawField(ContentX + Pad + ColId, TextY,
                       (ColName > ColId + 4u) ? ColName - ColId - 4u : 0, Tab[Ci].Id, Fg);
        StoreDrawField(ContentX + Pad + ColName, TextY,
                       (gStoreUiListRowW > ColName + 4u) ? gStoreUiListRowW - ColName : 0,
                       Tab[Ci].Title, Fg);
        RowY += LineH;
    }
    if (gStoreUiSbVisible && gStoreUiSbH > 0) {
        UiDrawScrollBar(gStoreUiSbX, gStoreUiSbY, gStoreUiSbW, gStoreUiSbH, gStoreUiScroll,
                        gStoreUiListVisible, gFiltCount);
    }
    if (gFiltCount == 0) {
        HalVideoDrawStringAt(ContentX + Pad, gStoreUiListTop + 4, LocaleString(MESSAGE_STORE_EMPTY),
                             ThemeTextMuted());
    }

    gBtnY = ListBottom + UI_LAYOUT_GAP;
    StoreBtnGeom(ContentX, ListW);
    DrawButtons();
    if (gStoreUiStatus[0]) {
        UINT32 StatusW = ListW > Pad * 2u ? ListW - Pad * 2u : ListW;
        char Status[96];
        int si;

        for (si = 0; gStoreUiStatus[si] && si < (int)sizeof(Status) - 1; si++) {
            Status[si] = gStoreUiStatus[si];
        }
        Status[si] = 0;
        UiFitTextUtf8(Status, StatusW);
        HalVideoDrawStringAt(ContentX + Pad, Cy + Ch - LineH - 2, Status, ThemeTextMuted());
    }

    if (gStoreUiPrevW > 0) {
        gStoreUiPrevX = ContentX + ListW;
        StorePaintDetail(gStoreUiPrevX, Cy, gStoreUiPrevW, Ch);
    } else {
        gStoreUiPrevX = ContentX + ListW;
        gStoreUiPrevW = 0;
    }

    if (gGuiWindowManager.FocusWin >= 0 &&
        gGuiWindowManager.FocusWin < MAX_WINS &&
        !gGuiWindowManager.WinBackupValid[gGuiWindowManager.FocusWin]) {
        BackupWindowAtEx(gGuiWindowManager.FocusWin, 1);
    } else {
        GuiBackupSyncRect(Cx, Cy, Cw, Ch);
    }
    HalVideoClearClip();
    GuiFrameBufferEnd();
}

