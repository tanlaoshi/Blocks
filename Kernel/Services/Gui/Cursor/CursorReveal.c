/*
 * CursorReveal.c — USB Reveal / 显隐 / 指针闲置（自 Cursor.c 纯搬）
 *
 * 契约：一露就必须流畅；慢活未完勿 Reveal。
 * 先试 Hw 再置 ready，避免 Compose 软画后 Enable 成双光标。
 */
#include "GuiPrivate.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Debug.h"

/* 0：Secondary 未完，不画/不 Present；Reveal 置 1 */
static int gCursorUsbReady;

/*
 * 最近一次指针位移（HalCpuTicks）；供钟栏/HotPoll 让路。
 * 勿改 HalMilliseconds：Arm64 软 tick≈100Hz 而 HalTicksPerSec=1000，
 * 绿基线闲置门限名义 400ms、实约数秒；真毫秒会过早放行 HotPoll/栏 Present，
 * Pi DWC2 闲置十余分钟鼠死（2026-11-08）。
 */
static UINT64 gPointerActivityTick;

int GuiCursorUsbReady(void) {
    return gCursorUsbReady ? 1 : 0;
}

void GuiCursorPaint(void) {
    GraphicsIrqEnter();
    CursorRestore();
    CursorPaint();
    GraphicsIrqLeave();
    GraphicsPresent();
}

void GuiCursorHide(void) {
    if (CursorHwActive()) {
        CursorHwDisable();
        return;
    }
    GraphicsIrqEnter();
    CursorRestore();
    GraphicsIrqLeave();
    GraphicsPresent();
}

void GuiCursorShow(void) {
    if (!gCursorUsbReady) {
        return;
    }
    CursorHwTryEnable();
    if (CursorHwActive()) {
        return;
    }
    GraphicsIrqEnter();
    CursorPaint();
    GraphicsIrqLeave();
    GraphicsPresent();
}

void GuiPointerNoteActivity(void) {
    gPointerActivityTick = HalCpuTicks(0);
}

int GuiPointerIdleForMilliseconds(UINT32 Ms) {
    UINT64 Now;
    UINT32 Tps;
    UINT64 Need;

    if (gPointerActivityTick == 0) {
        return 1;
    }
    Now = HalCpuTicks(0);
    Tps = HalTicksPerSec();
    if (Tps == 0) {
        Tps = 250;
    }
    Need = ((UINT64)Tps * (UINT64)Ms) / 1000ull;
    if (Need == 0) {
        Need = 1;
    }
    return (Now - gPointerActivityTick) >= Need ? 1 : 0;
}

void GuiCursorRevealAfterUsb(void) {
    UINT32 Cx;
    UINT32 Cy;

    if (gCursorUsbReady) {
        return;
    }
    Cx = gGuiWindowManager.ScreenWidth / 2;
    Cy = gGuiWindowManager.ScreenHeight / 2;
    if (gGuiWindowManager.ScreenWidth == 0) {
        Cx = 512;
    }
    if (gGuiWindowManager.ScreenHeight == 0) {
        Cy = 384;
    }
    gGuiWindowManager.CursorX = Cx;
    gGuiWindowManager.CursorY = Cy;
    gGuiWindowManager.CursorBtn = 0;
    gGuiWindowManager.MousePrevBtn = 0;
    /* 先放 Hold 再投递 IN，再清空队列对齐 Abs */
    HalInputMouseHoldUntilReveal(0);
    HalInputMouseHandoffDesktop(Cx, Cy);
    /*
     * 先试 Hw 再置 ready：避免 ready=1 后、Show 前 Compose 软画，
     * 随后 Enable 不擦 → 双光标（小=CUR，大=FB 残影）。
     */
    CursorHwTryEnable();
    gCursorUsbReady = 1;
    DebugWrite("Gui: cursor after usb secondary (center)\n");
    if (!CursorHwActive()) {
        GraphicsIrqEnter();
        CursorPaint();
        GraphicsIrqLeave();
        /* PresentCursor（含 Pi FB dcache sync）；勿只用 GraphicsPresent */
        HalVideoPresentCursor();
    }
}
