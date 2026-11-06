/*
 * Pointer.c — 商店悬停/按下（story-s4）
 *
 * 悬停 → StorePaintHoverDelta；分类/选行/钮 → 整窗或 ActDispatch。
 */
#include "StoreUiPrivate.h"

void StoreUiOnPointer(UINT32 X, UINT32 Y, UINT8 Buttons) {
    UINT32 Cx, Cy, Cw, Ch, Bg;
    int Side = -1;
    int Row = -1;
    int Btn = -1;
    int Need = 0;
    int FireBtn = -1;
    int FireSide = -1;
    int FireRow = -1;
    int OldSide;
    int OldRow;
    int OldBtn;
    int HoverOnly = 0;
    UINT32 Bx;
    int i;
    static UINT8 sPrevBtn;
    int Running = StoreJobIsRunning();

    if (!StoreUiIsFocused()) {
        if (gHoverSide >= 0 || gHoverRow >= 0 || gHoverBtn >= 0 || gPressBtn >= 0) {
            gHoverSide = -1;
            gHoverRow = -1;
            gHoverBtn = -1;
            gPressBtn = -1;
        }
        sPrevBtn = Buttons;
        return;
    }
    if (!GuiFocusClient(&Cx, &Cy, &Cw, &Ch, &Bg)) {
        sPrevBtn = Buttons;
        return;
    }

    /* 悬停只认客户区（对表 FilesUi） */
    if (X >= Cx && Y >= Cy && X < Cx + Cw && Y < Cy + Ch) {
        if (!Running && gStoreUiSideW > 0 && X < Cx + gStoreUiSideW &&
            Y >= gStoreUiSideRow0) {
            Side = (int)((Y - gStoreUiSideRow0) / gStoreUiSideLineH);
            if (Side < 0 || Side >= STORE_CAT_COUNT) {
                Side = -1;
            }
        } else if (Y >= gBtnY && Y < gBtnY + STORE_BTN_H) {
            for (i = 0; i < STORE_BTN_N; i++) {
                Bx = gBtnX0 + (UINT32)i * (gBtnW + STORE_BTN_GAP);
                if (X >= Bx && X < Bx + gBtnW) {
                    Btn = i;
                    break;
                }
            }
            if (Running && Btn > 0) {
                Btn = -1;
            }
        } else if (!Running && !(gStoreUiPrevW > 0 && X >= gStoreUiPrevX) &&
                   Y >= gStoreUiListTop && Y < gBtnY && gFiltCount > 0) {
            Row = UiListRowFromY(gStoreUiListTop, gStoreUiListLineH,
                                gStoreUiListVisible, Y);
            if (Row >= 0) {
                int Fi = gStoreUiScroll + Row;
                Row = (Fi >= 0 && Fi < gFiltCount) ? Fi : -1;
            }
        }
    }

    OldSide = gHoverSide;
    OldRow = gHoverRow;
    OldBtn = gHoverBtn;
    if (Side != gHoverSide || Row != gHoverRow || Btn != gHoverBtn) {
        gHoverSide = Side;
        gHoverRow = Row;
        gHoverBtn = Btn;
        if (!((Buttons & 1u) || (sPrevBtn & 1u))) {
            HoverOnly = 1;
        } else {
            Need = 1;
        }
    }
    if ((Buttons & 1u) && !(sPrevBtn & 1u)) {
        if (Btn >= 0) {
            gPressBtn = Btn;
            (void)StoreUiActDispatch(Btn, 1, 1);
            Need = 1;
        } else if (!Running && Side >= 0) {
            FireSide = Side;
        } else if (!Running && Row >= 0) {
            FireRow = Row;
        }
    } else if ((Buttons & 1u) && gPressBtn >= 0 && Btn != gPressBtn) {
        (void)StoreUiActDispatch(gPressBtn, 0, 0);
        gPressBtn = -1;
        Need = 1;
    } else if (!(Buttons & 1u) && (sPrevBtn & 1u) && gPressBtn >= 0) {
        if (Btn == gPressBtn) {
            FireBtn = gPressBtn;
        } else {
            (void)StoreUiActDispatch(gPressBtn, 0, 0);
        }
        gPressBtn = -1;
        Need = 1;
    }
    sPrevBtn = Buttons;

    if (FireSide >= 0) {
        gStoreUiCat = FireSide;
        gStoreUiScroll = 0;
        RebuildFilter();
        StoreSetStatus(StoreCatLabel(gStoreUiCat));
        StoreUiRepaint();
    } else if (FireRow >= 0) {
        gSel = FireRow;
        StoreUiRepaint();
    } else if (FireBtn >= 0) {
        (void)StoreUiActDispatch(FireBtn, 0, 1);
    } else if (HoverOnly) {
        StorePaintHoverDelta(OldSide, Side, OldRow, Row, OldBtn, Btn);
    } else if (Need) {
        StoreUiRepaint();
    }
}
