/*
 * StoreUi.c — 商店窗客户区：目录列表 + Install
 *
 * 【初学者】
 * - 分层：Core/Gui；数据 StoreLoadCatalog / StoreJobInstall
 * - 对外：StoreUiRefresh / StoreUiPaintClient / StoreUiClick
 * - 绘制由 WindowPaintFrame 调 StoreUiPaintClient
 */
#include "StoreUi.h"
#include "Store.h"
#include "StoreJob.h"
#include "Font.h"
#include "Locale.h"
#include "Theme.h"
#include "HalVideo.h"

#define ROW_HEIGHT 18u
#define INSTALL_BUTTON_WIDTH 72u
#define INSTALL_BUTTON_HEIGHT 22u

static UINT32 gClientX;
static UINT32 gClientY;
static UINT32 gClientW;
static UINT32 gClientH;
static STORE_ENTRY gTab[STORE_ENTRIES_MAX];
static int gCount;
static int gSel;
static char gStatus[48];

/*
 * SetStatus — 写底部状态行（截断到 gStatus 缓冲）
 *
 * 谁调用：StoreUiRefresh / StoreUiClick。
 */
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

/*
 * StoreUiRefresh — 重读 STORE.CAT 并同步 StoreJob 状态字
 *
 * 做什么：清 gCount；失败则 catalog fail；忙/ok/fail/ready 写入 gStatus。
 * 谁调用：WindowOpen(GUI_WIN_STORE)；Pointer 在 StoreJobStep 成功后。
 * 前后文：前 — StoreLoadCatalog；后 — StoreUiPaintClient 显示。
 */
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

/*
 * StoreUiPaintClient — 在商店窗客户区画列表与 Install
 *
 * 做什么：缓存 Cx..Ch；空目录且未刷新则懒调 StoreUiRefresh。
 * 谁调用：WindowPaintFrame（Kind==GUI_WIN_STORE）。
 */
void StoreUiPaintClient(UINT32 Cx, UINT32 Cy, UINT32 Cw, UINT32 Ch) {
    UINT32 i;
    UINT32 Y;
    UINT32 ButtonX;
    UINT32 ButtonY;

    gClientX = Cx;
    gClientY = Cy;
    gClientW = Cw;
    gClientH = Ch;
    if (Cw < 80u || Ch < 60u) {
        return;
    }
    if (gCount == 0 && gStatus[0] == 0) {
        StoreUiRefresh();
    }
    FontDrawStringAt(Cx + 10u, Cy + 8u, LocStr(MSG_STORE_HINT),
                     ThemeWindowTitleText());
    Y = Cy + 28u;
    for (i = 0; i < (UINT32)gCount && Y + ROW_HEIGHT < Cy + Ch - 36u; i++) {
        UINT32 Bg = ((int)i == gSel) ? ThemeWindowTitleBar() : ThemeWindowClient();
        HalVideoFillRect(Cx + 8u, Y, Cw - 16u, ROW_HEIGHT - 2u, Bg);
        FontDrawStringAt(Cx + 12u, Y + 2u, gTab[i].Id, ThemeWindowTitleText());
        FontDrawStringAt(Cx + 80u, Y + 2u, gTab[i].Title, ThemeWindowTitleText());
        Y += ROW_HEIGHT;
    }
    ButtonX = Cx + 10u;
    ButtonY = Cy + Ch - 30u;
    HalVideoFillRect(ButtonX, ButtonY, INSTALL_BUTTON_WIDTH, INSTALL_BUTTON_HEIGHT,
                     ThemeWindowTitleBar());
    FontDrawStringAt(ButtonX + 10u, ButtonY + 4u, "Install", ThemeWindowTitleText());
    if (gStatus[0]) {
        FontDrawStringAt(ButtonX + INSTALL_BUTTON_WIDTH + 12u, ButtonY + 4u, gStatus,
                         ThemeWindowTitleText());
    }
}

/*
 * StoreUiClick — 处理商店窗内左键（行选中 / Install）
 *
 * 做什么：Install 调 StoreJobInstall；行点击改 gSel。
 * 谁调用：Pointer（Hit==GUI_WIN_STORE）。
 * 返回：1 消费事件；0 未点中可交互区
 */
int StoreUiClick(INT32 X, INT32 Y) {
    UINT32 i;
    UINT32 RowY;
    UINT32 ButtonX;
    UINT32 ButtonY;
    int Error;

    if (gClientW < 80u || gClientH < 60u) {
        return 0;
    }
    ButtonX = gClientX + 10u;
    ButtonY = gClientY + gClientH - 30u;
    if (X >= (INT32)ButtonX && Y >= (INT32)ButtonY &&
        X < (INT32)(ButtonX + INSTALL_BUTTON_WIDTH) &&
        Y < (INT32)(ButtonY + INSTALL_BUTTON_HEIGHT)) {
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
    RowY = gClientY + 28u;
    for (i = 0; i < (UINT32)gCount && RowY + ROW_HEIGHT < gClientY + gClientH - 36u; i++) {
        if (X >= (INT32)(gClientX + 8u) && Y >= (INT32)RowY &&
            X < (INT32)(gClientX + gClientW - 8u) && Y < (INT32)(RowY + ROW_HEIGHT - 2u)) {
            gSel = (int)i;
            SetStatus(gTab[i].Id);
            return 1;
        }
        RowY += ROW_HEIGHT;
    }
    return 0;
}
