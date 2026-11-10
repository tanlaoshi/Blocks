/*
 * Pointer.c — 鼠标队列轮询与输入锁（PR-S3-guipointer-1）
 *
 * 唯一主人 GuiTask 调 GuiPollMouse（Shell 不调；runtime-a-poll）。
 *
 * Present 合并（runtime-b-present；真机队列常积几十包）：
 *   位移 — 只留末坐标，出队后一次 Move/拖更新（勿逐包 Present）
 *   边沿 — 按下/抬起逐包（点击、松手、Settings/Store/Edit）
 *   滚轮 — WheelSum 累加，出队后一次派发
 *   PresentBlocked — HoldDrain 只留末包，不合成（gui-g8-drag；见 PollHold.c）
 *   Defer — Push…Pop 包住 dequeue；拖图标内 Pause/Flush/Resume
 * 然后 DesktopTickClock（栏/图标/TTF 画）→ StoreUiPump。
 * MSC 热拔与钟采样在 Worker：DesktopWorkerTick（runtime-c-tick）。
 * 拖边界 / Slide vs Composite：见 Drag/ 下各 .c 头。长 IO 挪光标 Motion.c；点击 Click。
 * 边沿/合并位移/滚轮：PollMouseApply.c（PR-NORM-8）。
 */
#include "GuiPrivate.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Desktop.h"
#include "StoreUi.h"

void GuiInputLock(int Locked) {
    HAL_MOUSE_REPORT Raw;
    UINT8 LastBtn = gGuiWindowManager.MousePrevBtn;

    if (Locked) {
        gGuiWindowManager.InputLocked = 1;
        return;
    }
    /* 解锁前排空：热切期间堆积的边沿会在新分辨率下误点其它档 */
    if (HalMousePresent()) {
        while (HalMouseDequeue(&Raw)) {
            LastBtn = Raw.Buttons;
        }
    }
    gGuiWindowManager.MousePrevBtn = LastBtn;
    gGuiWindowManager.CursorBtn = LastBtn;
    gGuiWindowManager.InputLocked = 0;
}

int GuiInputLocked(void) {
    return gGuiWindowManager.InputLocked;
}

/* 从 XHCI 鼠标队列取报告并交给 GuiOnMouse（单一边沿/拖动逻辑） */
void GuiPollMouse(void) {
    HAL_MOUSE_REPORT Raw;
    UINT32 ScreenWidth;
    UINT32 ScreenHeight;
    UINT32 LastX;
    UINT32 LastY;
    UINT8 LastBtn;
    INT8 WheelSum;
    int Any;
    int NeedMove;

    if (!HalMousePresent()) {
        /*
         * 无鼠仍须 TickClock：否则 Worker/DHCP 已抬网托脏位，
         * Gui 永不画栏 → 壳 `ip` 有址、托盘一直红（Pi 2026-11-08）。
         */
        DesktopTickClock();
        StoreUiPump();
        return;
    }

    HalVideoGetSize(&ScreenWidth, &ScreenHeight);
    if (ScreenWidth == 0) {
        ScreenWidth = gGuiWindowManager.ScreenWidth
                          ? gGuiWindowManager.ScreenWidth
                          : 1024;
    }
    if (ScreenHeight == 0) {
        ScreenHeight = gGuiWindowManager.ScreenHeight
                           ? gGuiWindowManager.ScreenHeight
                           : 768;
    }

    /* PR-S-input-drain：drain 由 YieldForPollInput（稳态）+ StoreIoBreath（长 IO）负责；此处只 dequeue */
    if (gGuiWindowManager.InputLocked) {
        while (HalMouseDequeue(&Raw)) {
            gGuiWindowManager.MousePrevBtn = Raw.Buttons;
            gGuiWindowManager.CursorBtn = Raw.Buttons;
        }
        return;
    }

    /* USB 慢认未完：丢掉报告，不跟坐标；Reveal 后居中再动 */
    if (!GuiCursorUsbReady()) {
        while (HalMouseDequeue(&Raw)) {
        }
        /* 仍 TickClock：图标/壁纸 NeedRefresh 勿绑在 Reveal 之后 */
        DesktopTickClock();
        return;
    }

    /* 另一核正在写后缓冲：只留最后一包，勿再合成、勿 Present */
    if (GuiPresentBlocked()) {
        GuiPollHoldDrain(ScreenWidth, ScreenHeight);
        return;
    }

    /*
     * 真机：队列里常积几十份报告。逐条 CursorMove+Present → 光标极卡。
     * Defer Present，并合并位移；按键边沿/滚轮仍按每份报告处理。
     */
    LastX = gGuiWindowManager.CursorX;
    LastY = gGuiWindowManager.CursorY;
    LastBtn = gGuiWindowManager.MousePrevBtn;
    WheelSum = 0;
    Any = 0;
    NeedMove = 0;
    GuiPollHoldApply(&LastX, &LastY, &LastBtn, &WheelSum, &Any, &NeedMove);
    GuiPresentDeferPush();
    while (HalMouseDequeue(&Raw)) {
        UINT32 X;
        UINT32 Y;

        PollMouseMapReport(ScreenWidth, ScreenHeight, &Raw, &X, &Y);
        LastX = X;
        LastY = Y;
        NeedMove = 1;
        Any = 1;
        gGuiWindowManager.CursorBtn = Raw.Buttons;
        if (Raw.Wheel != 0) {
            WheelSum = (INT8)(WheelSum + Raw.Wheel);
        }
        /* 按下/抬起边沿：必须逐包看；拖动位移合并到队尾再 Update */
        PollMouseHandleButtonEdges(X, Y, Raw.Buttons, &LastBtn);
    }
    if (NeedMove) {
        PollMouseApplyMergedMove(LastX, LastY, LastBtn);
    }
    PollMouseApplyWheel(LastX, LastY, LastBtn, WheelSum);
    if (Any) {
        gGuiWindowManager.MousePrevBtn = LastBtn;
        gGuiWindowManager.CursorBtn = LastBtn;
    }
    if (NeedMove) {
        GuiPointerNoteActivity();
    }
    GuiPresentDeferPop();
    DesktopTickClock();
    /* 按钮抬起后排队的 Store 作业在此执行，OnPointer 内不再同步拷贝/删文件 */
    StoreUiPump();
}
