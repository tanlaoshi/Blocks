/*
 * StoreUiPaint.c — 左栏 / 列表 / 详情 / 底栏按钮
 * 核心：Ui.c
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
                UiDrawButtonEx(Bx, gBtnY, gBtnW, STORE_BTN_H, LocStr(MSG_STORE_CANCEL), Fg, Face,
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

static void PutPrefixed(char *Dst, int Max, const char *Pref, const char *Val) {
    int K = 0;

    if (!Dst || Max < 2) {
        return;
    }
    while (Pref && *Pref && K < Max - 1) {
        Dst[K++] = *Pref++;
    }
    while (Val && *Val && K < Max - 1) {
        Dst[K++] = *Val++;
    }
    Dst[K] = 0;
}

static void DrawDetail(UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    UINT32 LineH;
    UINT32 Ty;
    UINT32 MaxY;
    UINT32 MaxW;
    STORE_ENTRY *E;
    int Inst;
    char Line[80];
    int MapIdx;

    LineH = FontAdvanceY() + 2u;
    if (LineH < 14) {
        LineH = 14;
    }
    GfxFillRectangle(X, Y, W, H, ThemePanelDetailBackground());
    if (W > 3) {
        GfxFillRectangle(X, Y, 3, H, ThemePanelSeparator());
    }
    Ty = Y + 8;
    MaxY = Y + H - 4;
    MaxW = W > 20u ? W - 20u : W;
    Ty = UiDrawTextWrap(X + 10, Ty, MaxW, MaxY, LineH, LocStr(MSG_DEV_DETAIL), ThemeText());
    Ty += 4;

    E = SelectedEntry();
    MapIdx = (gSel >= 0 && gSel < gFiltCount) ? gMap[gSel] : -1;
    if (!E) {
        (void)UiDrawTextWrap(X + 10, Ty, MaxW, MaxY, LineH, LocStr(MSG_STORE_NO_SEL),
                             ThemeTextMuted());
        return;
    }
    Inst = CachedInstalled(MapIdx);
    Ty = UiDrawTextWrap(X + 10, Ty, MaxW, MaxY, LineH, E->Title[0] ? E->Title : E->Id,
                        ThemeText());
    PutPrefixed(Line, (int)sizeof(Line), LocStr(MSG_STORE_ID), E->Id);
    Ty = UiDrawTextWrap(X + 10, Ty, MaxW, MaxY, LineH, Line, ThemeTextMuted());
    PutPrefixed(Line, (int)sizeof(Line), LocStr(MSG_STORE_TYPE), E->Type);
    Ty = UiDrawTextWrap(X + 10, Ty, MaxW, MaxY, LineH, Line, ThemeTextMuted());
    if (E->Arch[0]) {
        PutPrefixed(Line, (int)sizeof(Line), LocStr(MSG_STORE_ARCH), E->Arch);
        Ty = UiDrawTextWrap(X + 10, Ty, MaxW, MaxY, LineH, Line, ThemeTextMuted());
    }
    if (E->File[0]) {
        PutPrefixed(Line, (int)sizeof(Line), LocStr(MSG_STORE_FILE), E->File);
        Ty = UiDrawTextWrap(X + 10, Ty, MaxW, MaxY, LineH, Line, ThemeTextMuted());
    }
    {
        char Repo[40];
        StoreUiFormatRepo(Repo, (int)sizeof(Repo));
        Ty = UiDrawTextWrap(X + 10, Ty, MaxW, MaxY, LineH, Repo, ThemeTextMuted());
    }
    (void)UiDrawTextWrap(X + 10, Ty, MaxW, MaxY, LineH,
                         Inst ? LocStr(MSG_STORE_INSTALLED) : LocStr(MSG_STORE_NOT_INST),
                         Inst ? ThemeTextAccent() : ThemeTextMuted());
}

static UINT32 StoreColAdv(void) {
    UINT32 Adv = FontAdvanceX();
    return (Adv < 8u) ? 8u : Adv;
}

/* 四列：已装、来源、编号、名称。宽度随字号变。 */
static void StoreColumns(UINT32 *Src, UINT32 *Id, UINT32 *Name, UINT32 *Need) {
    UINT32 Adv = StoreColAdv();

    *Src = Adv * 6u;
    *Id = *Src + Adv * 8u;
    *Name = *Id + Adv * 12u;
    *Need = *Name + Adv * 14u + UI_LAYOUT_PAD * 2u + STORE_SB_W + UI_LAYOUT_GAP;
}

static void StoreWidenList(UINT32 *Cw) {
    UINT32 SideW;
    UINT32 ListW;
    UINT32 DetailW;
    UINT32 Src;
    UINT32 Id;
    UINT32 Name;
    UINT32 Need;
    UINT32 Add;
    UINT32 MaxW;
    int Idx;
    int Grown;
    GUI_WINDOW *Win;

    Idx = gGuiWindowManager.FocusWin;
    if (Idx < 0 || Idx >= MAX_WINS) {
        return;
    }
    Win = &gGuiWindowManager.Windows[Idx];
    if (Win->Kind != GUI_WIN_STORE) {
        return;
    }
    MaxW = gGuiWindowManager.ScreenWidth;
    if (MaxW > 8u) {
        MaxW -= 8u;
    }
    StoreColumns(&Src, &Id, &Name, &Need);
    /*
     * 详情栏按客户区三分之一长。只补「缺口」一次，多出来的宽度会再被详情吃掉，
     * 列表仍短一截。连补几次，直到列表放下四列或碰到屏幕边。
     */
    Grown = 0;
    for (;;) {
        UiLayoutTriple(*Cw, &SideW, &ListW, &DetailW);
        if (ListW >= Need || Win->Width >= MaxW) {
            break;
        }
        Add = Need - ListW;
        if (Win->Width + Add > MaxW) {
            Add = MaxW - Win->Width;
        }
        if (Add == 0) {
            break;
        }
        Win->Width += Add;
        *Cw += Add;
        Grown = 1;
    }
    /* 加宽后旧备份尺寸不够，整窗作废，避免拖动时右侧透出后窗按钮 */
    if (Grown) {
        gGuiWindowManager.WinBackupValid[Idx] = 0;
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
            GfxFillRectangle(Win->X + 1, Win->Y + TITLE_HEIGHT, Win->Width - 2,
                            Win->Height - TITLE_HEIGHT - 1, Win->Background);
        }
        DrawWindowChromeAt(gGuiWindowManager.FocusWin);
    }
    GfxFillRectangle(Cx, Cy, Cw, Ch, Bg);
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
        GfxFillRectangle(Cx, Cy, SideW, Ch, ThemePanelSideBackground());
        if (SideW > UI_LAYOUT_SEP_W) {
            GfxFillRectangle(Cx + SideW - UI_LAYOUT_SEP_W, Cy, UI_LAYOUT_SEP_W, Ch,
                             ThemePanelSeparator());
        }
        HalVideoDrawStringAt(Cx + Pad, TitleY, LocStr(MSG_APP_STORE), ThemeText());
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
                   LocStr(MSG_STORE_COL_MARK), ThemeTextMuted());
    StoreDrawField(ContentX + Pad + ColSrc, TextY,
                   (ColId > ColSrc + 4u) ? ColId - ColSrc - 4u : 0,
                   LocStr(MSG_STORE_COL_SRC), ThemeTextMuted());
    StoreDrawField(ContentX + Pad + ColId, TextY,
                   (ColName > ColId + 4u) ? ColName - ColId - 4u : 0,
                   LocStr(MSG_STORE_COL_ID), ThemeTextMuted());
    StoreDrawField(ContentX + Pad + ColName, TextY,
                   (gStoreUiListRowW > ColName + 4u) ? gStoreUiListRowW - ColName : 0,
                   LocStr(MSG_STORE_COL_NAME), ThemeTextMuted());
    if (gStoreUiListRowW > 0) {
        GfxFillRectangle(ContentX + Pad, HeaderY + LineH - 1u, gStoreUiListRowW, 1u,
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
        Src = (Tab[Ci].Origin == STORE_SRC_NET) ? LocStr(MSG_STORE_SRC_NET)
                                                : LocStr(MSG_STORE_SRC_LOCAL);
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
        HalVideoDrawStringAt(ContentX + Pad, gStoreUiListTop + 4, LocStr(MSG_STORE_EMPTY),
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
        DrawDetail(gStoreUiPrevX, Cy, gStoreUiPrevW, Ch);
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
