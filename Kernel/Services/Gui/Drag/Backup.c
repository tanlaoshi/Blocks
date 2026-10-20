/*
 * GuiDragBackup.c — 拖动备份缓冲与起始抓屏
 * 核心：GuiDrag.c
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


int EnsureDragDirtyBuf(UINT32 Ww, UINT32 Hh) {
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


int EnsureUnderDragBuf(UINT32 Ww, UINT32 Wh) {
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


void CaptureDragRestoreData(int DragIdx) {
    UINT32 Pages;
    int i;

    gGuiWindowManager.ScreenSnapValid = 0;
    gGuiWindowManager.UnderDragValid = 0;
    if (DragIdx < 0 || DragIdx >= MAX_WINS || !gGuiWindowManager.Windows[DragIdx].Active) {
        return;
    }
    if (gGuiWindowManager.ScreenWidth == 0 || gGuiWindowManager.ScreenHeight == 0) {
        return;
    }

    gGuiWindowManager.DragStartX = gGuiWindowManager.Windows[DragIdx].X;
    gGuiWindowManager.DragStartY = gGuiWindowManager.Windows[DragIdx].Y;
    gGuiWindowManager.DragStartW = gGuiWindowManager.Windows[DragIdx].Width;
    gGuiWindowManager.DragStartH = gGuiWindowManager.Windows[DragIdx].Height;
    if (gGuiWindowManager.DragStartW == 0 || gGuiWindowManager.DragStartH == 0) {
        return;
    }

    Pages = BackupPageCount(gGuiWindowManager.ScreenWidth, gGuiWindowManager.ScreenHeight);
    if (gGuiWindowManager.ScreenSnap != 0 && gGuiWindowManager.ScreenSnapPages != Pages) {
        PhysicalMemoryFreePages(gGuiWindowManager.ScreenSnap, gGuiWindowManager.ScreenSnapPages);
        gGuiWindowManager.ScreenSnap = 0;
        gGuiWindowManager.ScreenSnapPages = 0;
    }
    if (gGuiWindowManager.ScreenSnap == 0) {
        gGuiWindowManager.ScreenSnap = (UINT32 *)PhysicalMemoryAllocatePages(Pages);
        if (gGuiWindowManager.ScreenSnap == 0) {
            DebugWrite("Gui: drag snap OOM — cancel\n");
            gGuiWindowManager.ScreenSnapValid = 0;
            return;
        }
        gGuiWindowManager.ScreenSnapPages = Pages;
    }
    /* 含被拖窗的全屏快照：非起始 footprint 露底时用 */
    HalVideoReadRect(0, 0, gGuiWindowManager.ScreenWidth, gGuiWindowManager.ScreenHeight, gGuiWindowManager.ScreenSnap);
    gGuiWindowManager.ScreenSnapValid = 1;
    EnsureDragDirtyBuf(gGuiWindowManager.ScreenWidth, gGuiWindowManager.ScreenHeight);

    if (!EnsureUnderDragBuf(gGuiWindowManager.DragStartW, gGuiWindowManager.DragStartH)) {
        return;
    }

    /*
     * 起始 footprint 的「去被拖窗」场景：暂时取消被拖窗 Active，重画桌面+其它窗，
     * 再读入 under-drag，最后恢复 Active 并贴回被拖窗。
     */
    HalVideoClearClip();
    gGuiWindowManager.Windows[DragIdx].Active = 0;
    DesktopFillRect(gGuiWindowManager.DragStartX, gGuiWindowManager.DragStartY, gGuiWindowManager.DragStartW, gGuiWindowManager.DragStartH);
    DesktopDrawRect(gGuiWindowManager.DragStartX, gGuiWindowManager.DragStartY, gGuiWindowManager.DragStartW, gGuiWindowManager.DragStartH);
    for (i = 0; i < MAX_WINS; i++) {
        if (!gGuiWindowManager.Windows[i].Active || i == DragIdx) {
            continue;
        }
        if (RectIntersects(gGuiWindowManager.Windows[i].X, gGuiWindowManager.Windows[i].Y, gGuiWindowManager.Windows[i].Width, gGuiWindowManager.Windows[i].Height,
                           gGuiWindowManager.DragStartX, gGuiWindowManager.DragStartY, gGuiWindowManager.DragStartW, gGuiWindowManager.DragStartH)) {
            /* 必须贴备份（含客户区文字），禁止 DrawWindowAt 空壳 */
            PaintWindowFromBackup(i);
        }
    }
    HalVideoReadRect(gGuiWindowManager.DragStartX, gGuiWindowManager.DragStartY, gGuiWindowManager.DragStartW, gGuiWindowManager.DragStartH, gGuiWindowManager.UnderDrag);
    gGuiWindowManager.UnderDragValid = 1;
    gGuiWindowManager.Windows[DragIdx].Active = 1;

    /* 贴回被拖窗（备份应在 Begin/Start 里已抓好） */
    if (gGuiWindowManager.WinBackupValid[DragIdx] && gGuiWindowManager.WinBackup[DragIdx] != 0) {
        HalVideoWriteRect(gGuiWindowManager.DragStartX, gGuiWindowManager.DragStartY, gGuiWindowManager.WinBackupW[DragIdx],
                          gGuiWindowManager.WinBackupH[DragIdx], gGuiWindowManager.WinBackup[DragIdx]);
        DrawWindowChromeAt(DragIdx);
    } else {
        DrawWindowAt(DragIdx);
    }

    /* 相交窗已从备份恢复，无需再 BackupWindowAt（以免读到瞬时脏 FB） */
}


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
    if (!EnsureWindowBackupBuf(DragIdx)) {
        DebugWrite("Gui: drag backup alloc failed\n");
        return;
    }
    HalVideoClearClip();
    for (i = 0; i < MAX_WINS; i++) {
        if (!gGuiWindowManager.Windows[i].Active) {
            continue;
        }
        EnsureWindowBackupBuf(i);
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

