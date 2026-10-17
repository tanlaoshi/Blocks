/*
 * GuiOpen.c — 开窗辅助 / 槽位（PR-S3-guiopen-1）
 *
 * 关窗见 GuiOpenClose.c；应用开窗见 GuiOpenApps.c。
 */
#include "GuiPrivate.h"
#include "UI.h"
#include "HalVideo.h"
#include "Debug.h"

/*
 * 开窗：Defer Present，先画满 chrome+客户区再备份，最后一次淡入。
 * 避免「空框 Present → 再填内容」闪白。
 */
void OpenChromeDefer(int Idx) {
    GuiPresentDeferPush();
    ComposeBeginEraseCursor();
    HalVideoClearClip();
    DrawWindowAt(Idx);
    ComposeEnd();
    gGuiWindowManager.FocusWin = Idx;
    RaiseWindow(Idx);
    SyncWindowVisuals();
}

void OpenFadeIn(int Idx) {
    BackupWindowAt(Idx);
    GuiFocusApply();
    BackupWindowAt(gGuiWindowManager.FocusWin);
    GuiAnimateWindowFade(gGuiWindowManager.FocusWin, 1);
    GuiPresentDeferPop();
}

/*
 * 单例应用：Settings / Store（及 Edit）已有则前置焦点，不新开。
 * Shell / Files 允许多开（各占一槽；Files 内容态仍为全局宿主，见 FilesUi）。
 */
int FocusExistingKind(GUI_WIN_KIND Kind, void (*Repaint)(void),
                             const char *LogTag) {
    int i;

    (void)LogTag;

    for (i = 0; i < MAX_WINS; i++) {
        if (gGuiWindowManager.Windows[i].Active && gGuiWindowManager.Windows[i].Kind == Kind) {
            gGuiWindowManager.FocusWin = i;
            RaiseWindow(i);
            SyncWindowVisuals();
            if (Repaint) {
                Repaint();
            }
            GuiFocusApply();
            BackupWindowAt(gGuiWindowManager.FocusWin);
            DebugWrite("Gui: focus existing ");
            DebugWrite(LogTag);
            DebugWrite(" idx=");
            DebugHex32((UINT32)gGuiWindowManager.FocusWin);
            DebugWrite("\n");
            return gGuiWindowManager.FocusWin;
        }
    }
    return -1;
}

int AllocWindowSlot(void) {
    int i;

    for (i = 0; i < MAX_WINS; i++) {
        /* Closing 时槽仍被关窗路径占用，勿复用 */
        if (!gGuiWindowManager.Windows[i].Active && !gGuiWindowManager.Windows[i].Closing) {
            return i;
        }
    }
    return -1;
}

void PlaceNewWindow(int Idx, UINT32 *OutX, UINT32 *OutY,
                           UINT32 *OutW, UINT32 *OutH) {
    UINT32 Margin = 48;
    UINT32 Cascade = (UINT32)Idx * 28;
    UINT32 W;
    UINT32 H;

    W = gGuiWindowManager.ScreenWidth > Margin * 2 + 200 ? gGuiWindowManager.ScreenWidth - Margin * 2 : gGuiWindowManager.ScreenWidth - 32;
    H = gGuiWindowManager.ScreenHeight > Margin * 2 + 120 ? gGuiWindowManager.ScreenHeight - Margin * 2 : gGuiWindowManager.ScreenHeight - 32;
    /* 默认桌面 1280×720：960×600 约 ~85 列×~28 行（8×16），ps 等长行不易裁切 */
    if (W > 960) {
        W = 960;
    }
    if (H > 600) {
        H = 600;
    }
    *OutX = Margin + Cascade;
    *OutY = Margin + Cascade;
    if (*OutX + W > gGuiWindowManager.ScreenWidth) {
        *OutX = Margin;
    }
    if (*OutY + H > gGuiWindowManager.ScreenHeight) {
        *OutY = Margin;
    }
    *OutW = W;
    *OutH = H;
}
