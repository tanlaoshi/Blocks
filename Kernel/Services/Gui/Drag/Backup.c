/*
 * Backup.c — 拖动备份缓冲与起始抓屏（gui-g8-drag）
 *
 * StartDragBackups：首帧 Armed 时抓屏/窗备份，置 DragHasBackup → 此后走 Slide。
 * UnderDrag / DragDirty / ScreenSnap 生命周期见 Drag.c ResetDragState。
 */
#include "GuiPrivate.h"
#include "UI.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Debug.h"
#include "PhysicalMemory.h"
#include "Desktop.h"
#include "Theme.h"
#include "SettingsUi.h"
#include "FilesUi.h"
#include "StoreUi.h"
#include "EditUi.h"


int EnsureDragDirtyBuffer(UINT32 Ww, UINT32 Hh) {
    UINT64 Need;
    UINT32 Pages;

    if (Ww == 0 || Hh == 0) {
        return 0;
    }
    Need = (UINT64)Ww * (UINT64)Hh;
    if (Need > 0xFFFFFFFFu / sizeof(UINT32)) {
        return 0;
    }
    if (gGuiWindowManager.DragDirty != 0 && gGuiWindowManager.DragDirtyCap >= (UINT32)Need) {
        return 1;
    }
    Pages = BackupPageCount(Ww, Hh);
    if (Pages == 0) {
        return 0;
    }
    if (gGuiWindowManager.DragDirty != 0) {
        PhysicalMemoryFreePages(gGuiWindowManager.DragDirty, gGuiWindowManager.DragDirtyPages);
        gGuiWindowManager.DragDirty = 0;
        gGuiWindowManager.DragDirtyPages = 0;
        gGuiWindowManager.DragDirtyCap = 0;
    }
    gGuiWindowManager.DragDirty = (UINT32 *)PhysicalMemoryAllocatePages(Pages);
    if (gGuiWindowManager.DragDirty == 0) {
        return 0;
    }
    gGuiWindowManager.DragDirtyPages = Pages;
    gGuiWindowManager.DragDirtyCap = (UINT32)(((UINT64)Pages * PAGE_SIZE) / sizeof(UINT32));
    return 1;
}


int EnsureUnderDragBuffer(UINT32 Ww, UINT32 Wh) {
    UINT32 Pages = BackupPageCount(Ww, Wh);

    if (Pages == 0) {
        return 0;
    }
    if (gGuiWindowManager.UnderDrag != 0 && gGuiWindowManager.UnderDragPages == Pages) {
        return 1;
    }
    if (gGuiWindowManager.UnderDrag != 0) {
        PhysicalMemoryFreePages(gGuiWindowManager.UnderDrag, gGuiWindowManager.UnderDragPages);
        gGuiWindowManager.UnderDrag = 0;
        gGuiWindowManager.UnderDragPages = 0;
    }
    gGuiWindowManager.UnderDragValid = 0;
    gGuiWindowManager.UnderDrag = (UINT32 *)PhysicalMemoryAllocatePages(Pages);
    if (gGuiWindowManager.UnderDrag == 0) {
        return 0;
    }
    gGuiWindowManager.UnderDragPages = Pages;
    return 1;
}


/*
 * CaptureDragRestoreData 已删（gui-drag-dead-read）：
 * 曾用逻辑坐标 HalVideoReadRect 抓 ScreenSnap/UnderDrag，全仓无调用方；
 * 接回会在 150% 烙字。活路：BeginDragBackups → BackupWindowAtEx → ReadRectPhys。
 */

void BeginDragBackups(int DragIdx) {
    int i;

    /*
     * 不用全屏 snap/under 合成：省 2× 帧缓冲，避免 OOM 半状态；
     * 也避免 snap 含窗本体时 Composite 露底失败留下标题栏/客户区残影
     * （见 44b1633；bbfa299 回潮后再现）。
     */
    ResetDragState();
    if (DragIdx < 0 || DragIdx >= MAX_WINS || !gGuiWindowManager.Windows[DragIdx].Active) {
        return;
    }
    if (!EnsureWindowBackupBuffer(DragIdx)) {
        DebugWrite("Gui: drag backup alloc failed\n");
        return;
    }
    HalVideoClearClip();
    for (i = 0; i < MAX_WINS; i++) {
        if (!gGuiWindowManager.Windows[i].Active) {
            continue;
        }
        EnsureWindowBackupBuffer(i);
        /*
         * ForceFull 只对「帧缓冲上可见」的窗：已被上层盖住时整窗 ReadRect
         * 会把前景烙进 gGuiWindowManager.WinBackup，ClearOld 露底时出现拖动烙印。
         * 遮挡窗走非 ForceFull，重叠像素保留先前干净备份。
         */
        BackupWindowAtEx(i, !WindowOccludedByOtherOrShadow(i));
    }
    if (!gGuiWindowManager.WinBackupValid[DragIdx]) {
        DebugWrite("Gui: drag backup invalid\n");
        return;
    }
    gGuiWindowManager.DragHasBackup = 1;
}


void StartDragBackups(int DragIdx) {
    /* G7：抓屏/合成不关中断，只锁光标擦除；ComposeBusy 防嵌套鼠标 */
    ComposeBegin();
    GraphicsIrqEnter();
    CursorRestore();
    HalVideoPresent();
    GraphicsIrqLeave();
    GuiFocusSave();
    BeginDragBackups(DragIdx);
    if (!gGuiWindowManager.DragHasBackup || !gGuiWindowManager.WinBackupValid[DragIdx]) {
        DebugWrite("Gui: drag aborted (no backup)\n");
        ResetDragState();
        gGuiWindowManager.DragWin = -1;
        ComposeEnd();
        return;
    }
    ComposeEnd();
}

