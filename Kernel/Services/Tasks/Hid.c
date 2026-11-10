/*
 * Hid.c — HID 报告送进控制台（input-f2-threshold）
 *
 * TasksPumpKeyboard：Count < HAL_INPUT_CPU_MIN → HalInputPoll（与 Yield 同门闩）；
 * ≥ MIN 只 dequeue（InputTask@CPU2 独占 poll）。清单 Tasks.c 头。
 * 焦点投递：FeedHidFocus.c（PR-NORM-7）。
 */
#include "Tasks.h"
#include "TasksPrivate.h"
#include "Hal.h"
#include "HalPlatform.h"
#include "HalVideo.h"
#include "HIDKeyboard.h"
#include "Console.h"
#include "Gui.h"
#include "EditUi.h"
#include "Debug.h"
#include "SerialLog.h"

/*
 * PR-19：按住方向键连续移动。
 * 按键报告只在「新按下」时交给 FeedHid，所以重复要靠每个 poll 周期自己再发：
 * 首次延迟 400ms（防误触），之后每 60ms 一次；键抬起（报告里没有这个码）
 * 就停。只对 Edit 生效，其它窗口不受影响。
 *
 * PR-19 修复：tick 不能只挂在 TasksPumpKeyboard 上——它只由用户态 poll 驱动，
 * Edit 是内核窗，按住期间没有新报告，于是永远不会再 tick（真机「只动一行」）。
 * 改成由 ShellTask 每圈无条件调用 TasksHidRepeatTick（见 Shell.c）：HID 报告
 * 本来就由 ShellTask dequeue，它每圈都走（无新键也走），tick 才有稳定节拍。
 * 「键仍按住」用 gLastReport：USB HID 按住时不重发报告，最后一份报告就是
 * 当前键状态，抬起报告会把它覆盖成空。
 */
static UINT8  gRepeatKey;
static UINT64 gRepeatNext;
static HAL_KEYBOARD_REPORT gLastReport;

static int HidKeyHeld(const HAL_KEYBOARD_REPORT *Report, UINT8 Key) {
    int j;

    if (Report == 0) {
        return 0;
    }
    for (j = 0; j < 6; j++) {
        if (Report->KeyCode[j] == Key) {
            return 1;
        }
    }
    return 0;
}

void HidRepeatArm(UINT8 Key) {
    UINT32 Hz = HalTicksPerSec();

    gRepeatKey = Key;
    gRepeatNext = HalCpuTicks(0) + (UINT64)Hz * 400u / 1000u;
}

static void HidRepeatTick(void) {
    UINT32 Hz;
    UINT64 Now;
    UINT64 Step;

    if (GuiInputLocked()) {
        gRepeatKey = 0;
        return;
    }
    if (gRepeatKey == 0) {
        return;
    }
    if (!HidKeyHeld(&gLastReport, gRepeatKey)) {
        gRepeatKey = 0;
        return;
    }
    if (!EditUiIsFocused()) {
        gRepeatKey = 0;
        return;
    }
    Hz = HalTicksPerSec();
    if (Hz == 0) {
        return;
    }
    Now = HalCpuTicks(0);
    if (Now < gRepeatNext) {
        return;
    }
    Step = (UINT64)Hz * 60u / 1000u;
    if (Step == 0) {
        Step = 1;
    }
    gRepeatNext = Now + Step;
    switch (gRepeatKey) {
    case HID_KEY_UP:
        EditUiOnArrow(0);
        break;
    case HID_KEY_DOWN:
        EditUiOnArrow(1);
        break;
    case HID_KEY_LEFT:
        EditUiOnArrowLeftRight(0);
        break;
    case HID_KEY_RIGHT:
        EditUiOnArrowLeftRight(1);
        break;
    default:
        break;
    }
}

void TasksPumpKeyboard(void) {
    static HAL_KEYBOARD_REPORT Prev;
    HAL_KEYBOARD_REPORT Report;

    /*
     * usb-boot-unplug 大窗：InputLocked 时只排空键队列，勿 FeedHid（禁 Esc/快捷键搅关机）。
     */
    if (GuiInputLocked()) {
        while (HalKeyboardDequeue(&Report)) {
        }
        return;
    }

    /*
     * Count≥HAL_INPUT_CPU_MIN：HalInputPoll 归 InputTask；此处只 dequeue。
     * Pi VirtDesktop：无 Input 专核，与 Yield 同门闩自 drain。
     * 用户态 sleep 钉 BSP 且不抢占时，Shell 同核转不到 → 须在 poll 路径自抽。
     */
    if (HalCpuCount() < HAL_INPUT_CPU_MIN ||
        (!HalCpuIsHypervisor() && HalPlatformUseVirtDesktop())) {
        HalInputPoll();
    }
    while (HalKeyboardDequeue(&Report)) {
        FeedHid(&Report, &Prev);
        Prev = Report;
    }
    /*
     * 这里不 tick：本函数只在用户态 poll 时被调，Edit（内核窗）按住期间
     * 没有新报告就不会再进来。重复节拍统一由 ShellTask 每圈
     * TasksHidRepeatTick 驱动；FeedHid 仍会更新 gLastReport。
     */
}

/* PR-19 修复：ShellTask 每圈无条件调用（无新 HID 报告也走），见 Shell.c 头。 */
void TasksHidRepeatTick(void) {
    HidRepeatTick();
}

void FeedHid(HAL_KEYBOARD_REPORT *Report, HAL_KEYBOARD_REPORT *Previous) {
    int Index;
    int PreviousIndex;
    UINT8 Key;
    int WasDown;

    /* 失根大窗 / 热更冻窗：任何入口勿投递（Shell 环曾漏检 InputLocked） */
    if (GuiInputLocked()) {
        return;
    }
    if (Report != 0) {
        /* 最近一份报告 = 当前按键状态（按住时 USB 不重发，抬起报告会覆盖） */
        gLastReport = *Report;
    }
    for (Index = 0; Index < 6; Index++) {
        Key = Report->KeyCode[Index];
        if (Key == 0) {
            continue;
        }
        WasDown = 0;
        for (PreviousIndex = 0; PreviousIndex < 6; PreviousIndex++) {
            if (Previous->KeyCode[PreviousIndex] == Key) {
                WasDown = 1;
                break;
            }
        }
        if (WasDown) {
            continue;
        }

        if (FeedHidTrySettings(Key, Report)) {
            continue;
        }
        if (FeedHidTryEdit(Key, Report)) {
            continue;
        }
        if (FeedHidTryTty(Key, Report)) {
            continue;
        }
        if (FeedHidTryStore(Key)) {
            continue;
        }
        if (FeedHidTryFiles(Key, Report)) {
            continue;
        }
        if (FeedHidTryUserWindow(Key)) {
            continue;
        }
        if (FeedHidTryStdinHold(Key, Report)) {
            continue;
        }
        FeedHidShellDefault(Key, Report);
    }
}
