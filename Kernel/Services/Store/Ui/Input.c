/*
 * Input.c — 点击、悬停、装卸队列（story-s4）
 *
 * Install/Remove/Sync → StoreJobEnqueue；Repo → 编辑 STORE_REPO_PATH。
 * StoreUiPump 空（Job 在 Worker）。核心全局：Ui.c。
 */
#include "StoreUiPrivate.h"

void StoreUiDoButton(int Btn) {
    STORE_ENTRY *E;
    char Buf[80];
    int i;
    int j;
    const char *Verb;

    if (StoreJobShellIsBusy()) {
        StoreSetStatus("shell busy");
        StoreUiRepaint();
        return;
    }
    if (StoreUiIsBusy() && !StoreJobIsBusy()) {
        /* 开窗 catalog 尚未就绪 */
        StoreSetStatus("...");
        return;
    }
    if (StoreJobIsBusy()) {
        if (Btn == 0) {
            if (StoreJobCancel() == 0) {
                StoreSetStatus("cancelling...");
            } else {
                StoreSetStatus("busy...");
            }
        } else {
            StoreSetStatus("busy...");
        }
        StoreUiRepaint();
        return;
    }
    gHoverBtn = -1;
    gPressBtn = -1;
    if (Btn == 3) {
        if (StoreUiWriteRepoFile() != 0) {
            StoreSetStatus("repo write fail");
            StoreUiRepaint();
            return;
        }
        if (GuiOpenEdit(STORE_REPO_PATH) < 0) {
            StoreSetStatus("repo edit fail");
        } else {
            StoreSetStatus("edit repo.txt, save, Sync");
        }
        StoreUiRepaint();
        return;
    }
    if (Btn == 2) {
        StoreUiApplyRepoFile();
        if (StoreJobEnqueue(STORE_JOB_SYNC, 0) != 0) {
            StoreSetStatus("busy...");
        } else {
            StoreSetStatus("sync: catalog");
        }
        StoreUiRepaint();
        return;
    }
    E = SelectedEntry();
    if (!E) {
        StoreSetStatus("no selection");
        StoreUiRepaint();
        return;
    }
    if (Btn == 0) {
        if (StoreIsInstalled(E->Id)) {
            StoreSetStatus("already installed");
            StoreUiRepaint();
            return;
        }
        Verb = "install";
        if (StoreJobEnqueue(STORE_JOB_INSTALL, E->Id) != 0) {
            StoreSetStatus("busy...");
            StoreUiRepaint();
            return;
        }
    } else {
        if (!StoreIsInstalled(E->Id)) {
            StoreSetStatus("not installed");
            StoreUiRepaint();
            return;
        }
        Verb = "remove";
        if (StoreJobEnqueue(STORE_JOB_REMOVE, E->Id) != 0) {
            StoreSetStatus("busy...");
            StoreUiRepaint();
            return;
        }
    }
    i = 0;
    while (Verb[i] && i < 24) {
        Buf[i] = Verb[i];
        i++;
    }
    Buf[i++] = ':';
    Buf[i++] = ' ';
    for (j = 0; E->Id[j] && i < 78; j++) {
        Buf[i++] = E->Id[j];
    }
    Buf[i] = 0;
    StoreSetStatus(Buf);
    StoreUiRepaint();
}

void StoreUiPump(void) {
    /*
     * Job 由 WorkerTask 推进（后台）。此处刻意不 Step：
     * 旧路径 GuiPollMouse→Pump→StoreRemove 会占死 Gui，装卸期鼠标必卡。
     */
}

void StoreUiOnClick(UINT32 X, UINT32 Y) {
    UINT32 Cx, Cy, Cw, Ch, Bg;
    int First;
    int Row;
    int i;
    UINT32 Bx;

    if (!StoreUiIsFocused()) {
        return;
    }
    if (!GuiFocusClient(&Cx, &Cy, &Cw, &Ch, &Bg)) {
        return;
    }
    if (X < Cx || Y < Cy || X >= Cx + Cw || Y >= Cy + Ch) {
        return;
    }

    if (gStoreUiSideW > 0 && X < Cx + gStoreUiSideW) {
        if (Y >= gStoreUiSideRow0) {
            Row = (int)((Y - gStoreUiSideRow0) / gStoreUiSideLineH);
            if (Row >= 0 && Row < STORE_CAT_COUNT) {
                gStoreUiCat = Row;
                gStoreUiScroll = 0;
                RebuildFilter();
                StoreSetStatus(StoreCatLabel(gStoreUiCat));
                StoreUiRepaint();
            }
        }
        return;
    }

    if (Y >= gBtnY && Y < gBtnY + STORE_BTN_H) {
        for (i = 0; i < STORE_BTN_N; i++) {
            Bx = gBtnX0 + (UINT32)i * (gBtnW + STORE_BTN_GAP);
            if (X >= Bx && X < Bx + gBtnW) {
                (void)StoreUiActDispatch(i, 1, 1);
                (void)StoreUiActDispatch(i, 0, 1);
                return;
            }
        }
    }

    if (gStoreUiSbVisible &&
        UiScrollBarHit(gStoreUiSbX, gStoreUiSbY, gStoreUiSbW, gStoreUiSbH, gStoreUiScroll, gStoreUiListVisible, gFiltCount,
                       X, Y, &First)) {
        gStoreUiScroll = First;
        StoreUiRepaint();
        return;
    }

    if (gStoreUiPrevW > 0 && X >= gStoreUiPrevX) {
        return;
    }
    if (Y >= gStoreUiListTop && Y < gBtnY && gFiltCount > 0) {
        Row = UiListRowFromY(gStoreUiListTop, gStoreUiListLineH, gStoreUiListVisible, Y);
        if (Row >= 0) {
            int Fi = gStoreUiScroll + Row;
            if (Fi >= 0 && Fi < gFiltCount) {
                gSel = Fi;
                StoreUiRepaint();
            }
        }
    }
}
