/*
 * Open.c — 开窗辅助 / 槽位（story-s1 / S1.3）
 *
 * InitWindowSlot / ClampWindowBox：Apps.c·Devices.c·Terminal.c 共用槽位样板。
 * OpenChromeDefer：Push Defer → 画 chrome → 调用方填客户区 → OpenFadeIn=Pop。
 * 必须配对 Pop，否则全局禁 Present（半帧/卡死）。门闩总表 Compose.c（gui-g1）。
 * FocusExistingKind：单例应用前置。应用入口 Apps.c；关窗 Close.c。
 */
#include "GuiPrivate.h"
#include "UI.h"
#include "HalVideo.h"
#include "Debug.h"

/*
 * 开窗：Push Defer → 画满 chrome+客户区 →（调用方）OpenFadeIn=Pop。
 * 避免「空框 Present → 再填内容」闪白。配对必须 Pop，否则全局禁 Present。
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
 * 单例应用：Settings / Store / Edit / Files / Devices / Tty 已有则前置，不新开。
 * Shell 允许多开（各占一槽）。
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

void ClampWindowBox(UINT32 Margin, UINT32 *W, UINT32 *H) {
    if (!W || !H) {
        return;
    }
    if (*W + Margin * 2u > gGuiWindowManager.ScreenWidth) {
        *W = gGuiWindowManager.ScreenWidth > Margin * 2u
                 ? gGuiWindowManager.ScreenWidth - Margin * 2u
                 : gGuiWindowManager.ScreenWidth / 2u;
    }
    if (*H + Margin * 2u > gGuiWindowManager.ScreenHeight) {
        *H = gGuiWindowManager.ScreenHeight > Margin * 2u
                 ? gGuiWindowManager.ScreenHeight - Margin * 2u
                 : gGuiWindowManager.ScreenHeight / 2u;
    }
}

void InitWindowSlot(int Idx, GUI_WIN_KIND Kind, UINT32 X, UINT32 Y, UINT32 W,
                    UINT32 H, UINT32 Background, const char *Title,
                    int PromptShown) {
    GUI_WINDOW *Win;

    if (Idx < 0 || Idx >= MAX_WINS) {
        return;
    }
    Win = &gGuiWindowManager.Windows[Idx];
    Win->Active = 1;
    Win->Kind = Kind;
    Win->X = X;
    Win->Y = Y;
    Win->Width = W;
    Win->Height = H;
    Win->Background = Background;
    Win->Title = Title;
    Win->TermSet = 0;
    Win->InputLen = 0;
    Win->WaitPrompt = 0;
    Win->PromptShown = PromptShown ? 1 : 0;
    Win->InputLine[0] = 0;
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
