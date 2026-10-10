/*
 * StoreUi.c — K48：商店窗薄 UI（目录 Gui/ 内不叠 Gui 前缀）
 *
 * 【初学者】列 STORE.CAT；点行选中；点 Install 走 StoreJobInstall。
 */
#include "StoreUi.h"
#include "Store.h"
#include "StoreJob.h"
#include "Font.h"
#include "Locale.h"
#include "Theme.h"
#include "HalVideo.h"

#define ROW_H 18u
#define BTN_W 72u
#define BTN_H 22u

static UINT32 gCx;
static UINT32 gCy;
static UINT32 gCw;
static UINT32 gCh;
static STORE_ENTRY gTab[STORE_ENTRIES_MAX];
static int gCount;
static int gSel;
static char gStatus[48];

static void SetStatus(const char *S) {
    int i = 0;

    if (S == 0) {
        gStatus[0] = 0;
        return;
    }
    while (S[i] && i < (int)sizeof(gStatus) - 1) {
        gStatus[i] = S[i];
        i++;
    }
    gStatus[i] = 0;
}

void StoreUiRefresh(void) {
    int Error;

    gCount = 0;
    gSel = 0;
    Error = StoreLoadCatalog(gTab, STORE_ENTRIES_MAX, &gCount);
    if (Error != STORE_OK) {
        gCount = 0;
        SetStatus("catalog fail");
        return;
    }
    if (gCount > 0) {
        if (gSel < 0 || gSel >= gCount) {
            gSel = 0;
        }
    } else {
        gSel = -1;
    }
    if (StoreJobIsBusy()) {
        SetStatus("busy");
    } else if (StoreJobLastError() == STORE_OK) {
        SetStatus("ok");
    } else if (StoreJobLastError() != 0) {
        SetStatus("fail");
    } else {
        SetStatus("ready");
    }
}

void StoreUiPaintClient(UINT32 Cx, UINT32 Cy, UINT32 Cw, UINT32 Ch) {
    UINT32 i;
    UINT32 Y;
    UINT32 Bx;
    UINT32 By;

    gCx = Cx;
    gCy = Cy;
    gCw = Cw;
    gCh = Ch;
    if (Cw < 80u || Ch < 60u) {
        return;
    }
    if (gCount == 0 && gStatus[0] == 0) {
        StoreUiRefresh();
    }
    FontDrawStringAt(Cx + 10u, Cy + 8u, LocStr(MSG_STORE_HINT),
                     ThemeWindowTitleText());
    Y = Cy + 28u;
    for (i = 0; i < (UINT32)gCount && Y + ROW_H < Cy + Ch - 36u; i++) {
        UINT32 Bg = ((int)i == gSel) ? ThemeWindowTitleBar() : ThemeWindowClient();
        HalVideoFillRect(Cx + 8u, Y, Cw - 16u, ROW_H - 2u, Bg);
        FontDrawStringAt(Cx + 12u, Y + 2u, gTab[i].Id, ThemeWindowTitleText());
        FontDrawStringAt(Cx + 80u, Y + 2u, gTab[i].Title, ThemeWindowTitleText());
        Y += ROW_H;
    }
    Bx = Cx + 10u;
    By = Cy + Ch - 30u;
    HalVideoFillRect(Bx, By, BTN_W, BTN_H, ThemeWindowTitleBar());
    FontDrawStringAt(Bx + 10u, By + 4u, "Install", ThemeWindowTitleText());
    if (gStatus[0]) {
        FontDrawStringAt(Bx + BTN_W + 12u, By + 4u, gStatus, ThemeWindowTitleText());
    }
}

int StoreUiClick(INT32 X, INT32 Y) {
    UINT32 i;
    UINT32 RowY;
    UINT32 Bx;
    UINT32 By;
    int Error;

    if (gCw < 80u || gCh < 60u) {
        return 0;
    }
    Bx = gCx + 10u;
    By = gCy + gCh - 30u;
    if (X >= (INT32)Bx && Y >= (INT32)By &&
        X < (INT32)(Bx + BTN_W) && Y < (INT32)(By + BTN_H)) {
        if (gSel < 0 || gSel >= gCount) {
            SetStatus("no sel");
            return 1;
        }
        Error = StoreJobInstall(gTab[gSel].Id);
        if (Error == STORE_BUSY) {
            SetStatus("busy");
            return 1;
        }
        if (Error == STORE_OK) {
            SetStatus("queued");
            return 1;
        }
        SetStatus("fail");
        return 1;
    }
    RowY = gCy + 28u;
    for (i = 0; i < (UINT32)gCount && RowY + ROW_H < gCy + gCh - 36u; i++) {
        if (X >= (INT32)(gCx + 8u) && Y >= (INT32)RowY &&
            X < (INT32)(gCx + gCw - 8u) && Y < (INT32)(RowY + ROW_H - 2u)) {
            gSel = (int)i;
            SetStatus(gTab[i].Id);
            return 1;
        }
        RowY += ROW_H;
    }
    return 0;
}
