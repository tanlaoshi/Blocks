/*
 * GuiInit.c — InitializeGui（开机模块表；与 GuiInitialize 同属 Services）
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
