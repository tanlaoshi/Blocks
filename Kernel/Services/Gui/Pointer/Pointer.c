/*
 * GuiPointer.c — 鼠标队列轮询与输入锁（PR-S3-guipointer-1）
 *
 * 长 IO 挪光标见 GuiPointerMotion.c；辅助 GuiClick / GuiPointerMouse。
 */
#include "GuiPrivate.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Debug.h"
#include "Desktop.h"
#include "SettingsUi.h"
#include "FilesUi.h"
#include "StoreUi.h"
#include "EditUi.h"
#include "Console.h"
#include "Process.h"
#include "SerialLog.h"
#include "Scheduler.h"

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
    UINT32 Sw;
    UINT32 Sh;
    GUI_MOUSE_STATE M;
    UINT32 LastX;
    UINT32 LastY;
    UINT8 LastBtn;
    INT8 WheelSum;
    int Any;
    int NeedMove;

    if (!HalMousePresent()) {
        return;
    }

    HalVideoGetSize(&Sw, &Sh);
    if (Sw == 0) {
        Sw = gGuiWindowManager.ScreenWidth ? gGuiWindowManager.ScreenWidth : 1024;
    }
    if (Sh == 0) {
        Sh = gGuiWindowManager.ScreenHeight ? gGuiWindowManager.ScreenHeight : 768;
    }

    /* PR-S-input-drain：drain 由 YieldForPollInput（稳态）+ StoreIoBreath（长 IO）负责；此处只 dequeue */
    if (gGuiWindowManager.InputLocked) {
        while (HalMouseDequeue(&Raw)) {
            gGuiWindowManager.MousePrevBtn = Raw.Buttons;
            gGuiWindowManager.CursorBtn = Raw.Buttons;
        }
        return;
    }

    /* 另一核正在写后缓冲：只留最后一包，勿再合成、勿 Present */
    if (GuiPresentBlocked()) {
        GuiPollHoldDrain(Sw, Sh);
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

        if (Raw.Absolute || Raw.X > 4096u || Raw.Y > 4096u) {
            X = (UINT32)((UINT64)Raw.X * (UINT64)Sw / 32767ull);
            Y = (UINT32)((UINT64)Raw.Y * (UINT64)Sh / 32767ull);
        } else {
            X = Raw.X;
            Y = Raw.Y;
        }
        if (X >= Sw) {
            X = Sw > 0 ? Sw - 1 : 0;
        }
        if (Y >= Sh) {
            Y = Sh > 0 ? Sh - 1 : 0;
        }

        LastX = X;
        LastY = Y;
        NeedMove = 1;
        Any = 1;
        gGuiWindowManager.CursorBtn = Raw.Buttons;
        if (Raw.Wheel != 0) {
            WheelSum = (INT8)(WheelSum + Raw.Wheel);
        }

        /* 按下/抬起边沿：必须逐包看；拖动位移合并到队尾再 Update */
        if ((Raw.Buttons & 1) && !(LastBtn & 1)) {
            GuiHandleClick(X, Y);
        }
        if (!(Raw.Buttons & 1) && (LastBtn & 1)) {
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
                    UINT32 Sx = gGuiWindowManager.SaveX;
                    UINT32 Sy = gGuiWindowManager.SaveY;
                    UINT32 SbW = gGuiWindowManager.SaveWidth;
                    UINT32 SbH = gGuiWindowManager.SaveHeight;

                    gGuiWindowManager.CursorVisible = 0;
                    if (SbW != 0 && SbH != 0) {
                        ClearIconFootprint(Sx, Sy, SbW, SbH);
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
        if ((Raw.Buttons & 2) && !(LastBtn & 2)) {
            GuiRightClickPlaceholder(X, Y);
        }
        /* Settings/Store：仅按键边沿逐包；位移悬停合并到队尾 GuiPointerMove */
        if (gGuiWindowManager.DragWin < 0 && gGuiWindowManager.ResizeWin < 0 && ((Raw.Buttons ^ LastBtn) & 1u)) {
            if (GuiFocusKind() == GUI_WIN_SETTINGS) {
                SettingsUiOnPointer(X, Y, Raw.Buttons);
            } else if (GuiFocusKind() == GUI_WIN_STORE) {
                StoreUiOnPointer(X, Y, Raw.Buttons);
            } else if (GuiFocusKind() == GUI_WIN_EDIT) {
                EditUiOnPointer(X, Y, Raw.Buttons);
            }
        }
        LastBtn = Raw.Buttons;
    }
    if (NeedMove) {
        if ((LastBtn & 1) && gGuiWindowManager.ResizeWin >= 0) {
            gGuiWindowManager.CursorX = LastX;
            gGuiWindowManager.CursorY = LastY;
            GuiResizeUpdate(LastX, LastY);
        } else if ((LastBtn & 1) && gGuiWindowManager.DragWin >= 0) {
            gGuiWindowManager.CursorX = LastX;
            gGuiWindowManager.CursorY = LastY;
            GuiDragUpdate(LastX, LastY);
        } else if ((LastBtn & 1) && DesktopIconDragActive()) {
            /*
             * 占 gDragFrame：Shell 核勿半帧 Present。
             * 禁止 CursorRestore：gGuiWindowManager.Under 常是「旧位置图标」，Overlay 写回后
             * 只进光标脏区 → 松手前一路图标残影。改为丢弃 under，壁纸清光标盒。
             */
            if (GuiDragFrameTry()) {
                UINT32 Sx = gGuiWindowManager.SaveX;
                UINT32 Sy = gGuiWindowManager.SaveY;
                UINT32 SbW = gGuiWindowManager.SaveWidth;
                UINT32 SbH = gGuiWindowManager.SaveHeight;

                GuiPresentDeferPause();
                if (gGuiWindowManager.CursorVisible) {
                    gGuiWindowManager.CursorVisible = 0;
                    if (SbW != 0 && SbH != 0) {
                        ClearIconFootprint(Sx, Sy, SbW, SbH);
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
    if (WheelSum != 0) {
        M.X = LastX;
        M.Y = LastY;
        M.Buttons = LastBtn;
        M.Wheel = WheelSum;
        if (GuiFocusKind() == GUI_WIN_FILES) {
            FilesUiOnWheel(M.Wheel);
        } else if (GuiFocusKind() == GUI_WIN_SHELL) {
            gConsole.OnWheel(M.Wheel);
        }
    }
    if (Any) {
        gGuiWindowManager.MousePrevBtn = LastBtn;
        gGuiWindowManager.CursorBtn = LastBtn;
    }
    GuiPresentDeferPop();
    DesktopTickClock();
    /* 按钮抬起后排队的 Store 作业在此执行，OnPointer 内不再同步拷贝/删文件 */
    StoreUiPump();
}

