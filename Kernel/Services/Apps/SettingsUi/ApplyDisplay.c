/*
 * ApplyDisplay.c — ApplyDisplayChoice（自 Apply.c 纯搬 · PR-NORM-16）
 */
#include "SettingsUiPrivate.h"

void ApplyDisplayChoice(int Index) {
    UINT32 W = 0;
    UINT32 H = 0;
    int Live = 0;
    int Saved = 0;
    static int sBusy;

    if (sBusy) {
        return;
    }
    sBusy = 1;
    GuiInputLock(1);

    if (Index == 0) {
        ThemeClearDisplayMode();
    } else if (Index >= 1 && Index <= ModeCount()) {
        W = gModes[Index - 1].W;
        H = gModes[Index - 1].H;
        ThemeSetDisplayMode(W, H);
    } else {
        GuiInputLock(0);
        sBusy = 0;
        return;
    }

    if (Index >= 1 && W != 0 && H != 0) {
        char Dim[24];
        FormatUxU(Dim, sizeof(Dim), W, H);
        HalConsoleWriteSerial("settings: apply ");
        HalConsoleWriteSerial(Dim);
        HalConsoleWriteSerial("\n");
    }

    /* 先落盘。QEMU 的 vvfat 在热切之后再写盘会断言退出，模式也存不住。 */
    if (ThemeSave() == 0) {
        Saved = 1;
    } else {
        HalConsoleWriteSerial("settings: display save failed\n");
        DebugWrite("settings: display save failed\n");
    }

    if (Index >= 1 && W != 0 && H != 0 && !HalCpuIsHypervisor()) {
        if (ThemeApplyDisplayLive(W, H) == 0) {
            Live = 1;
            HalConsoleWriteSerial("settings: display applied live\n");
            DebugWrite("settings: display applied live\n");
        }
    }

    if (Live) {
        gDisplayHint = 2;
        HalConsoleWriteSerial(
            "settings: live OK; cold boot still needs quit QEMU + ./run-split.sh\n");
    } else if (Saved) {
        gDisplayHint = 1;
        if (HalCpuIsHypervisor()) {
        HalConsoleWriteSerial(
            "settings: display saved; quit QEMU, then run (EDID follows BLOCKS.DB)\n");
            DebugWrite("settings: display pref saved (relaunch QEMU on VM)\n");
        } else {
            HalConsoleWriteSerial(
                "settings: display pref saved; reboot real PC to apply BLOCKS.DB\n");
            DebugWrite("settings: display pref saved (real PC reboot)\n");
        }
    } else {
        gDisplayHint = 0;
    }
    GuiInputLock(0);
    sBusy = 0;
    PaintMenu();
}
