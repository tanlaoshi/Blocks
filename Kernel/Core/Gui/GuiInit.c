/*
 * GuiInit.c — InitializeGui（开机模块编织）
 *
 * 人话：读主题/字体/语言后开桌面合成。实现仍在 Services/Gui*（阶段 B 再并）。
 */
#include "KernelModulesPrivate.h"
#include "Hal.h"
#include "Gui.h"
#include "Font.h"
#include "Theme.h"
#include "Db.h"
#include "Locale.h"

static void PollInput(void) {
    if (!HalCpuIsHypervisor()) {
        HalInputPoll();
    }
}

static void DrainMouse(void) {
    HAL_MOUSE_REPORT Mouse;

    if (HalCpuIsHypervisor()) {
        return;
    }
    HalInputPoll();
    while (HalMouseDequeue(&Mouse)) {
    }
}

int InitializeGui(void) {
    PollInput();
    (void)DbInitialize();
    (void)FontLoadAssets();
    (void)FontTtfLoad();
    (void)FontTtfInit();
    PollInput();
    (void)ThemeLoad();
    LocaleInitialize();
    PollInput();
    if (ThemeUiScale() != 100) {
        (void)HalVideoSetUiScale(ThemeUiScale());
    }
    GuiInitialize();
    DrainMouse();
    return 0;
}
