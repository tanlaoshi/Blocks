/*
 * GuiPollHold.c — 拖帧进行中排空鼠标，帧后再补边沿
 */
#include "GuiPrivate.h"
#include "Hal.h"
#include "HalVideo.h"
#include "Desktop.h"
#include "Scheduler.h"

static UINT32 gHoldX;
static UINT32 gHoldY;
static UINT8 gHoldBtn;
static INT8 gHoldWheel;
static int gHoldValid;

static void RememberMouse(UINT32 Sw, UINT32 Sh, const HAL_MOUSE_REPORT *Raw) {
    UINT32 X;
    UINT32 Y;

    if (Raw->Absolute || Raw->X > 4096u || Raw->Y > 4096u) {
        X = (UINT32)((UINT64)Raw->X * (UINT64)Sw / 32767ull);
        Y = (UINT32)((UINT64)Raw->Y * (UINT64)Sh / 32767ull);
    } else {
        X = Raw->X;
        Y = Raw->Y;
    }
    if (X >= Sw) {
        X = Sw > 0 ? Sw - 1 : 0;
    }
    if (Y >= Sh) {
        Y = Sh > 0 ? Sh - 1 : 0;
    }
    gHoldX = X;
    gHoldY = Y;
    gHoldBtn = Raw->Buttons;
    if (Raw->Wheel != 0) {
        gHoldWheel = (INT8)(gHoldWheel + Raw->Wheel);
    }
    gHoldValid = 1;
}

void GuiPollHoldDrain(UINT32 Sw, UINT32 Sh) {
    HAL_MOUSE_REPORT Raw;

    while (HalMouseDequeue(&Raw)) {
        RememberMouse(Sw, Sh, &Raw);
    }
}

void GuiPollHoldApply(UINT32 *LastX, UINT32 *LastY, UINT8 *LastBtn,
                      INT8 *WheelSum, int *Any, int *NeedMove) {
    UINT32 X;
    UINT32 Y;
    UINT8 Btn;
    UINT8 Prev;

    if (!gHoldValid || !LastX || !LastY || !LastBtn || !WheelSum || !Any || !NeedMove) {
        return;
    }
    X = gHoldX;
    Y = gHoldY;
    Btn = gHoldBtn;
    Prev = *LastBtn;
    *WheelSum = gHoldWheel;
    gHoldWheel = 0;
    gHoldValid = 0;
    *LastX = X;
    *LastY = Y;
    *NeedMove = 1;
    *Any = 1;
    gGuiWindowManager.CursorBtn = Btn;
    if ((Btn & 1) && !(Prev & 1)) {
        GuiHandleClick(X, Y);
    }
    if (!(Btn & 1) && (Prev & 1)) {
        int WasIcon = DesktopIconDragActive();
        UINTN Wait = 0;

        GuiResizeEnd();
        GuiDragEnd();
        if (WasIcon) {
            while (!GuiDragFrameTry()) {
                HalCpuRelax();
                if ((++Wait & 0x3FFFu) == 0) {
                    (void)SchedulerCondResched();
                }
            }
            GuiPresentDeferPause();
            DesktopIconDragEnd();
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
            GfxIrqEnter();
            CursorPaint();
            HalVideoPresentFlush();
            GfxIrqLeave();
            GuiPresentDeferResume();
            GuiDragFrameLeave();
        }
    }
    if ((Btn & 2) && !(Prev & 2)) {
        GuiRightClickPlaceholder(X, Y);
    }
    *LastBtn = Btn;
}
