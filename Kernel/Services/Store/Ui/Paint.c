/*
 * StoreUiPaint.c — 左栏 / 列表 / 详情 / 底栏按钮
 * 核心：Ui.c
 */
#include "StoreUiPrivate.h"
#include "GuiPrivate.h"

/* StorePaintList / Widen / DrawButtons → PaintList.c（PR-NORM-16）。 */


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

void StorePaintDetail(UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
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
    GraphicsFillRectangle(X, Y, W, H, ThemePanelDetailBackground());
    if (W > 3) {
        GraphicsFillRectangle(X, Y, 3, H, ThemePanelSeparator());
    }
    Ty = Y + 8;
    MaxY = Y + H - 4;
    MaxW = W > 20u ? W - 20u : W;
    Ty = UiDrawTextWrap(X + 10, Ty, MaxW, MaxY, LineH, LocaleString(MESSAGE_DEV_DETAIL), ThemeText());
    Ty += 4;

    E = SelectedEntry();
    MapIdx = (gSel >= 0 && gSel < gFiltCount) ? gMap[gSel] : -1;
    if (!E) {
        (void)UiDrawTextWrap(X + 10, Ty, MaxW, MaxY, LineH, LocaleString(MESSAGE_STORE_NO_SEL),
                             ThemeTextMuted());
        return;
    }
    Inst = CachedInstalled(MapIdx);
    Ty = UiDrawTextWrap(X + 10, Ty, MaxW, MaxY, LineH, E->Title[0] ? E->Title : E->Id,
                        ThemeText());
    PutPrefixed(Line, (int)sizeof(Line), LocaleString(MESSAGE_STORE_ID), E->Id);
    Ty = UiDrawTextWrap(X + 10, Ty, MaxW, MaxY, LineH, Line, ThemeTextMuted());
    PutPrefixed(Line, (int)sizeof(Line), LocaleString(MESSAGE_STORE_TYPE), E->Type);
    Ty = UiDrawTextWrap(X + 10, Ty, MaxW, MaxY, LineH, Line, ThemeTextMuted());
    if (E->Arch[0]) {
        PutPrefixed(Line, (int)sizeof(Line), LocaleString(MESSAGE_STORE_ARCH), E->Arch);
        Ty = UiDrawTextWrap(X + 10, Ty, MaxW, MaxY, LineH, Line, ThemeTextMuted());
    }
    if (E->File[0]) {
        PutPrefixed(Line, (int)sizeof(Line), LocaleString(MESSAGE_STORE_FILE), E->File);
        Ty = UiDrawTextWrap(X + 10, Ty, MaxW, MaxY, LineH, Line, ThemeTextMuted());
    }
    {
        char Repo[40];
        StoreUiFormatRepo(Repo, (int)sizeof(Repo));
        Ty = UiDrawTextWrap(X + 10, Ty, MaxW, MaxY, LineH, Repo, ThemeTextMuted());
    }
    (void)UiDrawTextWrap(X + 10, Ty, MaxW, MaxY, LineH,
                         Inst ? LocaleString(MESSAGE_STORE_INSTALLED) : LocaleString(MESSAGE_STORE_NOT_INST),
                         Inst ? ThemeTextAccent() : ThemeTextMuted());
}

