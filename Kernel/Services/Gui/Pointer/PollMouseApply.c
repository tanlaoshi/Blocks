/*
 * PollMouseApply.c — GuiPollMouse 边沿 / 合并位移 / 滚轮（自 Pointer.c 纯搬 · PR-NORM-8）
 *
 * 勿半截关泵鼠；Present/Defer/鼠优先契约仍由 GuiPollMouse 编排。
 */
#include "GuiPrivate.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Desktop.h"
#include "SettingsUi.h"
#include "FilesUi.h"
#include "StoreUi.h"
#include "EditUi.h"
#include "Console.h"
#include "Scheduler.h"

void PollMouseMapReport(UINT32 ScreenWidth, UINT32 ScreenHeight,
                        const HAL_MOUSE_REPORT *Raw, UINT32 *OutX,
                        UINT32 *OutY) {
    UINT32 X;
    UINT32 Y;

    if (Raw->Absolute || Raw->X > 4096u || Raw->Y > 4096u) {
        X = (UINT32)((UINT64)Raw->X * (UINT64)ScreenWidth / 32767ull);
        Y = (UINT32)((UINT64)Raw->Y * (UINT64)ScreenHeight / 32767ull);
    } else {
        X = Raw->X;
        Y = Raw->Y;
    }
    if (X >= ScreenWidth) {
        X = ScreenWidth > 0 ? ScreenWidth - 1 : 0;
    }
    if (Y >= ScreenHeight) {
        Y = ScreenHeight > 0 ? ScreenHeight - 1 : 0;
    }
    *OutX = X;
    *OutY = Y;
}

void PollMouseHandleButtonEdges(UINT32 X, UINT32 Y, UINT8 Buttons,
                                UINT8 *LastBtn) {
    if ((Buttons & 1) && !(*LastBtn & 1)) {
        /*
         * 本函数外层 DeferPush：关窗轻量路径须能真正 Present。
         * 与图标拖一致 Pause，避免 Close 画完仍被 Defer 吞掉。
         */
        GuiPresentDeferPause();
        GuiHandleClick(X, Y);
        GuiPresentDeferResume();
    }
    if (!(Buttons & 1) && (*LastBtn & 1)) {
        int WasIconDrag = DesktopIconDragActive();
        UINTN Wait = 0;

        GuiResizeEnd();
        GuiDragEnd();
        if (WasIconDrag) {
            /* 与窗拖一致：等帧锁再归位 Present，防 Shell 核半帧 Present 拖尾 */
            while (!GuiDragFrameTry()) {
                HalCpuRelax();
                if ((++Wait & 0x3FFFu) == 0) {
                    (void)SchedulerCondResched();
                }
            }
            GuiPresentDeferPause();
            DesktopIconDragEnd();
            /*
             * End 已擦落点并画在吸附格。若仍 Visible，CursorPaint→Restore
             * 会把拖动末帧 gGuiWindowManager.Under（旧落点图标）吐回 → 仅松手残影。
             */
            if (gGuiWindowManager.CursorVisible) {
                UINT32 SaveX = gGuiWindowManager.SaveX;
                UINT32 SaveY = gGuiWindowManager.SaveY;
                UINT32 SaveWidth = gGuiWindowManager.SaveWidth;
                UINT32 SaveHeight = gGuiWindowManager.SaveHeight;

                gGuiWindowManager.CursorVisible = 0;
                if (SaveWidth != 0 && SaveHeight != 0) {
                    ClearIconFootprint(SaveX, SaveY, SaveWidth, SaveHeight);
                }
            }
            GraphicsIrqEnter();
            CursorPaint();
            HalVideoPresentFlush();
            GraphicsIrqLeave();
            GuiPresentDeferResume();
            GuiDragFrameLeave();
        }
    }
    if ((Buttons & 2) && !(*LastBtn & 2)) {
        GuiRightClickPlaceholder(X, Y);
    }
    /* Settings/Store：仅按键边沿逐包；位移悬停合并到队尾 GuiPointerMove */
    if (gGuiWindowManager.DragWin < 0 && gGuiWindowManager.ResizeWin < 0 &&
        ((Buttons ^ *LastBtn) & 1u)) {
        if (GuiFocusKind() == GUI_WIN_SETTINGS) {
            SettingsUiOnPointer(X, Y, Buttons);
        } else if (GuiFocusKind() == GUI_WIN_STORE) {
            StoreUiOnPointer(X, Y, Buttons);
        } else if (GuiFocusKind() == GUI_WIN_EDIT) {
            EditUiOnPointer(X, Y, Buttons);
        }
    }
    *LastBtn = Buttons;
}

void PollMouseApplyMergedMove(UINT32 LastX, UINT32 LastY, UINT8 LastBtn) {
    if ((LastBtn & 1) && gGuiWindowManager.ResizeWin >= 0) {
        /*
         * 须先 CursorMove：硬件光标只写 CUR_POS；若只改 CursorX 再
         * Resize/Drag，窗跟上了、箭头仍停在旧位 →「追赶」。
         * 软路径 CursorMove 在 Drag 时会藏光标（Visible=0）。
         */
        CursorMove(LastX, LastY);
        GuiResizeUpdate(LastX, LastY);
    } else if ((LastBtn & 1) && gGuiWindowManager.DragWin >= 0) {
        CursorMove(LastX, LastY);
        GuiDragUpdate(LastX, LastY);
    } else if ((LastBtn & 1) && DesktopIconDragActive()) {
        /*
         * 占 gDragFrame：Shell 核勿半帧 Present。
         * 禁止 CursorRestore：gGuiWindowManager.Under 常是「旧位置图标」，Overlay 写回后
         * 只进光标脏区 → 松手前一路图标残影。改为丢弃 under，壁纸清光标盒。
         */
        if (GuiDragFrameTry()) {
            UINT32 SaveX = gGuiWindowManager.SaveX;
            UINT32 SaveY = gGuiWindowManager.SaveY;
            UINT32 SaveWidth = gGuiWindowManager.SaveWidth;
            UINT32 SaveHeight = gGuiWindowManager.SaveHeight;

            GuiPresentDeferPause();
            if (gGuiWindowManager.CursorVisible) {
                gGuiWindowManager.CursorVisible = 0;
                if (SaveWidth != 0 && SaveHeight != 0) {
                    ClearIconFootprint(SaveX, SaveY, SaveWidth, SaveHeight);
                }
            }
            gGuiWindowManager.CursorX = LastX;
            gGuiWindowManager.CursorY = LastY;
            DesktopIconDragUpdate(LastX, LastY);
            GraphicsIrqEnter();
            CursorPaint();
            HalVideoPresentFlush();
            GraphicsIrqLeave();
            GuiPresentDeferResume();
            GuiDragFrameLeave();
        }
    } else {
        GuiPointerMove(LastX, LastY);
    }
}

void PollMouseApplyWheel(UINT32 LastX, UINT32 LastY, UINT8 LastBtn,
                         INT8 WheelSum) {
    GUI_MOUSE_STATE Mouse;

    if (WheelSum == 0) {
        return;
    }
    Mouse.X = LastX;
    Mouse.Y = LastY;
    Mouse.Buttons = LastBtn;
    Mouse.Wheel = WheelSum;
    if (GuiFocusKind() == GUI_WIN_FILES) {
        FilesUiOnWheel(Mouse.Wheel);
    } else if (GuiFocusKind() == GUI_WIN_EDIT) {
        EditUiOnWheel(Mouse.Wheel);
    } else if (GuiFocusKind() == GUI_WIN_SHELL) {
        gConsole.OnWheel(Mouse.Wheel);
    }
}
