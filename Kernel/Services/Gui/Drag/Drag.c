/*
 * Drag.c — 标题栏拖动生命周期（gui-g8-drag）
 *
 * 移窗 Move.c；备份 Backup.c；帧画 Composite.c / Slide.c。
 *
 * ── 拖动 vs 合成柱边界（对表拧）──
 *
 * 合成柱不得插手拖中画面：
 *   GuiComposeDamage：GuiDragActive()（DragWin|ResizeWin）→ 早退
 *   （脏标记可积；松手后 Refresh 再画。拖中由 Move/Slide 自管 Present）
 *
 * 多核 Present 互斥：
 *   gDragFrame（Slide.c Try/Leave）| ComposeBusy → GuiPresentBlocked
 *   Pointer：Blocked 时 GuiPollHoldDrain 只留末包，不 Move/不合成
 *   Shell 环末：!GuiPresentBlocked 才 HalVideoPresent（G6）
 *
 * 热路径帧画：DragHasBackup → RedrawDragFrameSlide（CopyRect+擦条+Flush）
 *   RedrawDragFrame（Composite）仍在库中；ClearOldDragFootprint 等公用
 *   Move 在有备份时走 Slide，勿混两条热路径（闪/残影难对）
 *
 * 生命周期：Armed→StartDragBackups→Update(持 Frame)→End→ResetDragState
 *   Reset 勿清 WinBackupValid（单击标题也会 End）。
 */
#include "GuiPrivate.h"
#include "UI.h"
#include "HalVideo.h"
#include "Hal.h"
#include "HalDevices.h"
#include "Debug.h"
#include "PhysicalMemory.h"
#include "Desktop.h"
#include "Theme.h"
#include "SettingsUi.h"
#include "FilesUi.h"
#include "StoreUi.h"
#include "DevicesUi.h"
#include "EditUi.h"
#include "TtyUi.h"
#include "Scheduler.h"

void ResetDragState(void) {
    /*
     * 只清拖动合成用的 snap/under/dirty。
     * 绝不能清 gGuiWindowManager.WinBackupValid：单击标题栏也会进 GuiDragEnd，若无重叠会走这里，
     * 清掉后备份后 SyncWindowVisuals 只能 DrawWindowAt，Settings/Shell 文字全没。
     */
    if (gGuiWindowManager.ScreenSnap != 0) {
        PhysicalMemoryFreePages(gGuiWindowManager.ScreenSnap, gGuiWindowManager.ScreenSnapPages);
        gGuiWindowManager.ScreenSnap = 0;
        gGuiWindowManager.ScreenSnapPages = 0;
    }
    if (gGuiWindowManager.UnderDrag != 0) {
        PhysicalMemoryFreePages(gGuiWindowManager.UnderDrag, gGuiWindowManager.UnderDragPages);
        gGuiWindowManager.UnderDrag = 0;
        gGuiWindowManager.UnderDragPages = 0;
    }
    if (gGuiWindowManager.DragDirty != 0) {
        PhysicalMemoryFreePages(gGuiWindowManager.DragDirty, gGuiWindowManager.DragDirtyPages);
        gGuiWindowManager.DragDirty = 0;
        gGuiWindowManager.DragDirtyPages = 0;
        gGuiWindowManager.DragDirtyCap = 0;
    }
    gGuiWindowManager.ScreenSnapValid = 0;
    gGuiWindowManager.UnderDragValid = 0;
    gGuiWindowManager.DragStartW = 0;
    gGuiWindowManager.DragStartH = 0;
    gGuiWindowManager.DragHasBackup = 0;
}

int AnyWindowsOverlap(void) {
    int i;
    int j;

    for (i = 0; i < MAX_WINS; i++) {
        if (!gGuiWindowManager.Windows[i].Active) {
            continue;
        }
        for (j = i + 1; j < MAX_WINS; j++) {
            if (!gGuiWindowManager.Windows[j].Active) {
                continue;
            }
            if (RectIntersects(gGuiWindowManager.Windows[i].X, gGuiWindowManager.Windows[i].Y, gGuiWindowManager.Windows[i].Width, gGuiWindowManager.Windows[i].Height,
                               gGuiWindowManager.Windows[j].X, gGuiWindowManager.Windows[j].Y, gGuiWindowManager.Windows[j].Width, gGuiWindowManager.Windows[j].Height)) {
                return 1;
            }
        }
    }
    return 0;
}

void GuiDragUpdate(UINT32 X, UINT32 Y) {
    INT32 Nx;
    INT32 Ny;
    INT32 Dx;
    INT32 Dy;
    UINT32 PinX;
    UINT32 PinY;
    GUI_WINDOW *W;

    if (gGuiWindowManager.DragWin < 0 || gGuiWindowManager.DragWin >= MAX_WINS || !gGuiWindowManager.Windows[gGuiWindowManager.DragWin].Active) {
        return;
    }
    W = &gGuiWindowManager.Windows[gGuiWindowManager.DragWin];
    Nx = (INT32)X - gGuiWindowManager.DragOffX;
    Ny = (INT32)Y - gGuiWindowManager.DragOffY;
    ClampWindowPos(W, &Nx, &Ny);

    Dx = Nx - (INT32)W->X;
    Dy = Ny - (INT32)W->Y;
    if (Dx < 0) {
        Dx = -Dx;
    }
    if (Dy < 0) {
        Dy = -Dy;
    }
    if ((UINT32)Dx >= DRAG_MIN_STEP || (UINT32)Dy >= DRAG_MIN_STEP) {
        if (GuiDragFrameTry()) {
            /* 持 gDragFrame 期间勿被抢：否则同核 Shell 自旋等锁会饿死本任务 */
            SchedulerPreemptDisable();
            if (gGuiWindowManager.DragArmed) {
                StartDragBackups(gGuiWindowManager.DragWin);
                gGuiWindowManager.DragArmed = 0;
                if (gGuiWindowManager.DragWin < 0 || !gGuiWindowManager.DragHasBackup) {
                    SchedulerPreemptEnable();
                    GuiDragFrameLeave();
                    goto PinCursor;
                }
            }
            MoveWindowTo(gGuiWindowManager.DragWin, (UINT32)Nx, (UINT32)Ny);
            SchedulerPreemptEnable();
            GuiDragFrameLeave();
        }
    }

PinCursor:
    /*
     * 钉死：光标始终停在「当前窗位 + 按下时标题栏偏移」。
     * 窗贴边后鼠继续走，若不回钉，箭头会滑离当初点按处。
     */
    W = &gGuiWindowManager.Windows[gGuiWindowManager.DragWin];
    if (gGuiWindowManager.DragWin < 0 || !W->Active) {
        return;
    }
    {
        INT32 Px = (INT32)W->X + gGuiWindowManager.DragOffX;
        INT32 Py = (INT32)W->Y + gGuiWindowManager.DragOffY;

        if (Px < 0) {
            Px = 0;
        }
        if (Py < 0) {
            Py = 0;
        }
        if ((UINT32)Px >= gGuiWindowManager.ScreenWidth && gGuiWindowManager.ScreenWidth > 0) {
            Px = (INT32)gGuiWindowManager.ScreenWidth - 1;
        }
        if ((UINT32)Py >= gGuiWindowManager.ScreenHeight && gGuiWindowManager.ScreenHeight > 0) {
            Py = (INT32)gGuiWindowManager.ScreenHeight - 1;
        }
        PinX = (UINT32)Px;
        PinY = (UINT32)Py;
    }
    if (gGuiWindowManager.CursorX == PinX && gGuiWindowManager.CursorY == PinY) {
        return;
    }
    gGuiWindowManager.CursorX = PinX;
    gGuiWindowManager.CursorY = PinY;
    if (CursorHwActive()) {
        HalVideoHwCursorSetPos(PinX, PinY);
    }
    HalInputMouseHandoffDesktop(PinX, PinY);
}

void GuiDragEnd(void) {
    int DragIdx;
    int DidDrag;
    int Top;
    UINTN Wait = 0;

    while (!GuiDragFrameTry()) {
        HalCpuRelax();
        /* 勿空转饿死持锁核（NUC 多核：GuiTask 持帧、Shell 松手等锁） */
        if ((++Wait & 0x3FFFu) == 0) {
            (void)SchedulerCondResched();
        }
    }
    DragIdx = gGuiWindowManager.DragWin;
    DidDrag = gGuiWindowManager.DragHasBackup;
    gGuiWindowManager.DragWin = -1;
    gGuiWindowManager.DragArmed = 0;
    Top = DragIdx;
    /* 与 GuiDragUpdate 一致：-1 哨兵 + 上界，避免 -Warray-bounds */
    if (DragIdx >= 0 && DragIdx < MAX_WINS && gGuiWindowManager.Windows[DragIdx].Active) {
        if (DidDrag) {
            INT32 Nx = (INT32)gGuiWindowManager.CursorX - gGuiWindowManager.DragOffX;
            INT32 Ny = (INT32)gGuiWindowManager.CursorY - gGuiWindowManager.DragOffY;

            ClampWindowPos(&gGuiWindowManager.Windows[DragIdx], &Nx, &Ny);
            MoveWindowTo(DragIdx, (UINT32)Nx, (UINT32)Ny);
            RaiseWindow(DragIdx);
            /* Raise 可能搬槽：后续重画/备份必须用 gGuiWindowManager.FocusWin（同 GuiRaiseToFront） */
            Top = gGuiWindowManager.FocusWin;
            /*
             * 残影：拖动路径上旧 chrome 落在「当前窗矩形之外」，只贴窗擦不掉。
             * 先铺桌面再按备份贴回，清轨迹；空色块若已烙进备份则随后 Settings/Shell 重画补。
             */
            SyncWindowVisualsEx(1);
            GuiFocusApply();
            /*
             * Sync 会把上层阴影画到下层露出区（正确）。下层备份保持干净：
             * 被挡像素不吸入上层；移走后下次 Sync/ClearOld 用备份重贴即无烙印。
             */
        }
        if (Top < 0 || Top >= MAX_WINS || !gGuiWindowManager.Windows[Top].Active) {
            Top = -1;
        }
        if (Top >= 0 && gGuiWindowManager.Windows[Top].Kind == GUI_WIN_SETTINGS) {
            SettingsUiRepaint();
        } else if (Top >= 0 && gGuiWindowManager.Windows[Top].Kind == GUI_WIN_STORE) {
            StoreUiRepaint();
        } else if (Top >= 0 && gGuiWindowManager.Windows[Top].Kind == GUI_WIN_DEVICES) {
            DevicesUiRepaint();
        } else if (Top >= 0 && gGuiWindowManager.Windows[Top].Kind == GUI_WIN_FILES) {
            FilesUiRepaint();
        } else if (Top >= 0 && gGuiWindowManager.Windows[Top].Kind == GUI_WIN_EDIT) {
            EditUiRepaint();
        } else if (Top >= 0 && gGuiWindowManager.Windows[Top].Kind == GUI_WIN_TTY) {
            TtyUiRepaint();
        } else if (Top >= 0 && gGuiWindowManager.Windows[Top].Kind == GUI_WIN_USER) {
            PaintUserClient(Top);
        } else if (Top >= 0 && gGuiWindowManager.Windows[Top].Kind == GUI_WIN_SHELL &&
                   !gGuiWindowManager.WinBackupValid[Top]) {
            GuiConsoleOpsOnShellOpened();
        }
        /* 清桌面合成后：无备份的 Shell 客户区是空壳，补画控制台 */
        if (DidDrag) {
            int i;

            for (i = 0; i < MAX_WINS; i++) {
                if (gGuiWindowManager.Windows[i].Active && gGuiWindowManager.Windows[i].Kind == GUI_WIN_SHELL &&
                    !gGuiWindowManager.WinBackupValid[i]) {
                    int Prev = gGuiWindowManager.FocusWin;

                    gGuiWindowManager.FocusWin = i;
                    GuiConsoleOpsOnShellOpened();
                    gGuiWindowManager.FocusWin = Prev;
                }
            }
        }
        if (Top >= 0 && gGuiWindowManager.Windows[Top].Active) {
            BackupWindowAt(Top);
        }
    }
    if (!AnyWindowsOverlap()) {
        ResetDragState();
    }
    GuiDragFrameLeave();
    if (DidDrag) {
        GuiDragFrameLog();
    }
}
