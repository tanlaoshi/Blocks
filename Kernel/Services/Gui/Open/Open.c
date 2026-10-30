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
    /* PR-layout：按可用区（去任务栏）与「同屏两窗」预算，不再只看整屏 */
    GuiPlaceSizeBudget(Margin, W, H);
}

void InitWindowSlot(int Idx, GUI_WIN_KIND Kind, UINT32 X, UINT32 Y, UINT32 W,
                    UINT32 H, UINT32 Background, const char *Title,
                    int PromptShown) {
    GUI_WINDOW *Win;

    if (Idx < 0 || Idx >= MAX_WINS) {
        return;
    }
    /* PR-layout：先按尺寸预算与级联/回绕找不重叠位置，再写槽位 */
    GuiPlaceWindow(Idx, &X, &Y, &W, &H);
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

/* Shell 默认窗上限：960×600 ≈ 85 列×28 行（8×16 点阵），ps 长行不裁切 */
#define SHELL_MAX_WIDTH  960u
#define SHELL_MAX_HEIGHT 600u

void PlaceNewWindow(int Idx, UINT32 *OutX, UINT32 *OutY,
                           UINT32 *OutW, UINT32 *OutH) {
    UINT32 Margin = GUI_PLACE_EDGE;
    UINT32 W;
    UINT32 H;

    (void)Idx;
    W = gGuiWindowManager.ScreenWidth > Margin * 2u + GUI_PLACE_MIN_WIDTH
            ? gGuiWindowManager.ScreenWidth - Margin * 2u
            : GUI_PLACE_MIN_WIDTH;
    H = gGuiWindowManager.ScreenHeight > Margin * 2u + GUI_PLACE_MIN_HEIGHT
            ? gGuiWindowManager.ScreenHeight - Margin * 2u
            : GUI_PLACE_MIN_HEIGHT;
    if (W > SHELL_MAX_WIDTH) {
        W = SHELL_MAX_WIDTH;
    }
    if (H > SHELL_MAX_HEIGHT) {
        H = SHELL_MAX_HEIGHT;
    }
    GuiPlaceSizeBudget(Margin, &W, &H);
    /* 首选左上；真正的级联/回绕在 InitWindowSlot → GuiPlaceWindow */
    *OutX = Margin;
    *OutY = Margin;
    *OutW = W;
    *OutH = H;
}
