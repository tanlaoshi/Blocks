/*
 * ComposePresentGate.c — GraphicsPresent / Defer Push·Pop·Pause（从 Compose.c 拆）
 */
#include "GuiPrivate.h"
#include "HalVideo.h"

/* 主题合成中推迟 Present；拖动等路径仍立即提交 */
void GraphicsPresent(void) {
    if (gGuiWindowManager.DeferPresent) {
        gGuiWindowManager.ShellEchoCoalesce = 0;
        return;
    }
    /* 热更/失根 InputLocked：禁局部 Present 凿冻窗（右下角彩斑，2026-11-04） */
    if (gGuiWindowManager.InputLocked) {
        return;
    }
    /*
     * PR-G-shell-present：Shell 打字回显先写后缓冲，跳过本帧 Present。
     * ShellTask 每轮末尾 HalVideoPresent 会合并刷出；help/ls 仍走 Defer*。
     */
    if (gGuiWindowManager.ShellEchoCoalesce) {
        gGuiWindowManager.ShellEchoCoalesce = 0;
        return;
    }
    HalVideoPresent();
}

void GuiPresentShellEchoMark(void) {
    gGuiWindowManager.ShellEchoCoalesce = 1;
}

void GuiPresentDeferPush(void) {
    gGuiWindowManager.DeferPresent++;
}

void GuiPresentDeferPop(void) {
    if (gGuiWindowManager.DeferPresent > 0) {
        gGuiWindowManager.DeferPresent--;
    }
    gGuiWindowManager.ShellEchoCoalesce = 0;
    if (gGuiWindowManager.DeferPresent == 0 &&
        gGuiWindowManager.InputLocked == 0) {
        HalVideoPresent();
    }
}

/* 可嵌套：仅最外层把计数清零；Resume 还原。供 Shell 等 StoreJob 时放行光标 Present */
static int gDeferPauseDepth;
static int gDeferSaved;

void GuiPresentDeferPause(void) {
    if (gDeferPauseDepth++ == 0) {
        gDeferSaved = gGuiWindowManager.DeferPresent;
        gGuiWindowManager.DeferPresent = 0;
        gGuiWindowManager.ShellEchoCoalesce = 0;
    }
}

void GuiPresentDeferResume(void) {
    if (gDeferPauseDepth > 0 && --gDeferPauseDepth == 0) {
        gGuiWindowManager.DeferPresent = gDeferSaved;
        gDeferSaved = 0;
    }
}
