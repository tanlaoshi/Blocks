/*
 * GuiBackup.c — 窗备份缓冲（核心）
 * 辅助：GuiBackupSample.c
 *
 * 从 GuiBackup.c 单体迁出；只搬家、不改逻辑。
 */
#include "GuiPrivate.h"
#include "UI.h"
#include "HalVideo.h"
#include "Hal.h"
#include "PhysicalMemory.h"
#include "Theme.h"
#include "Font.h"
#include "Desktop.h"

int AllActiveWindowsHaveValidBackup(void) {
    int i;

    for (i = 0; i < MAX_WINS; i++) {
        if (gGuiWindowManager.Windows[i].Active && !gGuiWindowManager.WinBackupValid[i]) {
            return 0;
        }
    }
    return 1;
}


UINT32 BackupPageCount(UINT32 Ww, UINT32 Wh) {
    UINT64 Bytes = (UINT64)Ww * (UINT64)Wh * sizeof(UINT32);

    return (UINT32)((Bytes + PAGE_SIZE - 1) / PAGE_SIZE);
}


int EnsureWindowBackupBuf(int Idx) {
    UINT32 Pages;

    if (Idx < 0 || Idx >= MAX_WINS || !gGuiWindowManager.Windows[Idx].Active) {
        return 0;
    }
    if (gGuiWindowManager.Windows[Idx].Width == 0 || gGuiWindowManager.Windows[Idx].Height == 0) {
        return 0;
    }
    Pages = BackupPageCount(gGuiWindowManager.Windows[Idx].Width, gGuiWindowManager.Windows[Idx].Height);
    if (Pages == 0) {
        return 0;
    }
    if (gGuiWindowManager.WinBackup[Idx] != 0 && gGuiWindowManager.WinBackupPages[Idx] == Pages) {
        return 1;
    }
    if (gGuiWindowManager.WinBackup[Idx] != 0) {
        PhysicalMemoryFreePages(gGuiWindowManager.WinBackup[Idx], gGuiWindowManager.WinBackupPages[Idx]);
        gGuiWindowManager.WinBackup[Idx] = 0;
        gGuiWindowManager.WinBackupPages[Idx] = 0;
    }
    /* 换新页后内容未定义，必须清 Valid，禁止未填充就 WriteRect */
    gGuiWindowManager.WinBackupValid[Idx] = 0;
    gGuiWindowManager.WinBackup[Idx] = (UINT32 *)PhysicalMemoryAllocatePages(Pages);
    if (gGuiWindowManager.WinBackup[Idx] == 0) {
        return 0;
    }
    gGuiWindowManager.WinBackupPages[Idx] = Pages;
    return 1;
}


void PreallocWindowBackups(void) {
    int i;

    for (i = 0; i < MAX_WINS; i++) {
        if (gGuiWindowManager.Windows[i].Active) {
            EnsureWindowBackupBuf(i);
        }
    }
}


/*
 * ForceFull：主题自下而上刚画完本窗、上层尚未覆盖时，必须整窗 ReadRect，
 * 否则 Occluded 路径会跳过重叠区，备份镂空，透视桌面/抬窗花屏。
 */
void BackupWindowAtEx(int Idx, int ForceFull) {
    const GUI_WINDOW *Win = &gGuiWindowManager.Windows[Idx];
    UINT32 Rw;
    UINT32 Rh;
    UINT32 Bw;
    int HadValid;
    UINT32 *OldBuf;
    UINT32 Row;
    UINT32 Col;
    int WasVisible;

    if (Idx < 0 || Idx >= MAX_WINS || !Win->Active) {
        return;
    }
    if (Win->Width == 0 || Win->Height == 0) {
        return;
    }
    Rw = Win->Width;
    Rh = Win->Height;
    if (Win->X + Rw > gGuiWindowManager.ScreenWidth) {
        Rw = gGuiWindowManager.ScreenWidth - Win->X;
    }
    if (Win->Y + Rh > gGuiWindowManager.ScreenHeight) {
        Rh = gGuiWindowManager.ScreenHeight - Win->Y;
    }
    if (Rw == 0 || Rh == 0) {
        return;
    }

    HadValid = gGuiWindowManager.WinBackupValid[Idx];
    OldBuf = gGuiWindowManager.WinBackup[Idx];
    if (!EnsureWindowBackupBuf(Idx)) {
        gGuiWindowManager.WinBackupValid[Idx] = 0;
        return;
    }
    if (gGuiWindowManager.WinBackup[Idx] != OldBuf) {
        HadValid = 0;
    }

    WasVisible = gGuiWindowManager.CursorVisible;
    ComposeBegin();
    GfxIrqEnter();
    CursorRestore();
    GfxIrqLeave();

    /*
     * ForceFull：compose 自下而上、上层尚未画时整窗 ReadRect（含将被盖住的区域）。
     * 非 ForceFull 且被挡（含上层阴影扫过）：只更新可见像素，被挡处保留旧备份。
     */
    if (!ForceFull && WindowOccludedByOtherOrShadow(Idx)) {
        UINT32 OldBw = gGuiWindowManager.WinBackupW[Idx];
        UINT32 OldBh = gGuiWindowManager.WinBackupH[Idx];

        if (!HadValid) {
            gGuiWindowManager.WinBackupValid[Idx] = 0;
            if (WasVisible) {
                GfxIrqEnter();
                CursorPaint();
                GfxIrqLeave();
            }
            ComposeEnd();
            return;
        }
        Bw = Rw;
        for (Row = 0; Row < Rh; Row++) {
            for (Col = 0; Col < Rw; Col++) {
                UINT32 Px = Win->X + Col;
                UINT32 Py = Win->Y + Row;
                UINT32 *Dst = &gGuiWindowManager.WinBackup[Idx][Row * Bw + Col];

                if (PixelCoveredByHigherWindow(Idx, Px, Py)) {
                    if (OldBuf != gGuiWindowManager.WinBackup[Idx] && Col < OldBw && Row < OldBh) {
                        *Dst = OldBuf[Row * OldBw + Col];
                    }
                    continue;
                }
                *Dst = HalVideoReadPixel(Px, Py);
            }
        }
        gGuiWindowManager.WinBackupW[Idx] = Rw;
        gGuiWindowManager.WinBackupH[Idx] = Rh;
        gGuiWindowManager.WinBackupValid[Idx] = 1;
        if (WasVisible) {
            GfxIrqEnter();
            CursorPaint();
            GfxIrqLeave();
        }
        ComposeEnd();
        return;
    }

    HalVideoReadRect(Win->X, Win->Y, Rw, Rh, gGuiWindowManager.WinBackup[Idx]);
    gGuiWindowManager.WinBackupW[Idx] = Rw;
    gGuiWindowManager.WinBackupH[Idx] = Rh;
    gGuiWindowManager.WinBackupValid[Idx] = 1;
    if (WasVisible) {
        GfxIrqEnter();
        CursorPaint();
        GfxIrqLeave();
    }
    ComposeEnd();
}


void BackupWindowAt(int Idx) {
    BackupWindowAtEx(Idx, 0);
}


void PaintWindowFromBackup(int Idx) {
    const GUI_WINDOW *Win = &gGuiWindowManager.Windows[Idx];

    if (!Win->Active) {
        return;
    }
    if (gGuiWindowManager.WinBackupValid[Idx] && gGuiWindowManager.WinBackup[Idx] != 0) {
        HalVideoWriteRect(Win->X, Win->Y, gGuiWindowManager.WinBackupW[Idx], gGuiWindowManager.WinBackupH[Idx],
                       gGuiWindowManager.WinBackup[Idx]);
        DrawWindowChromeAt(Idx);
        /* 阴影由 SyncWindowVisualsEx 第二遍统一画，避免先画影再被下层 WriteRect 打乱 */
        return;
    }
    DrawWindowAt(Idx);
}


void ShiftWinBackupsUp(int From, int To) {
    UINT32 *Buf = gGuiWindowManager.WinBackup[From];
    UINT32 Bw = gGuiWindowManager.WinBackupW[From];
    UINT32 Bh = gGuiWindowManager.WinBackupH[From];
    UINT32 Bp = gGuiWindowManager.WinBackupPages[From];
    int Bv = gGuiWindowManager.WinBackupValid[From];
    int J;

    for (J = From; J < To; J++) {
        gGuiWindowManager.WinBackup[J] = gGuiWindowManager.WinBackup[J + 1];
        gGuiWindowManager.WinBackupW[J] = gGuiWindowManager.WinBackupW[J + 1];
        gGuiWindowManager.WinBackupH[J] = gGuiWindowManager.WinBackupH[J + 1];
        gGuiWindowManager.WinBackupPages[J] = gGuiWindowManager.WinBackupPages[J + 1];
        gGuiWindowManager.WinBackupValid[J] = gGuiWindowManager.WinBackupValid[J + 1];
    }
    gGuiWindowManager.WinBackup[To] = Buf;
    gGuiWindowManager.WinBackupW[To] = Bw;
    gGuiWindowManager.WinBackupH[To] = Bh;
    gGuiWindowManager.WinBackupPages[To] = Bp;
    gGuiWindowManager.WinBackupValid[To] = Bv;
}


int RectIntersects(UINT32 Ax, UINT32 Ay, UINT32 Aw, UINT32 Ah,
                          UINT32 Bx, UINT32 By, UINT32 Bw, UINT32 Bh) {
    if (Aw == 0 || Ah == 0 || Bw == 0 || Bh == 0) {
        return 0;
    }
    return Ax < Bx + Bw && Bx < Ax + Aw && Ay < By + Bh && By < Ay + Ah;
}


void ClipRectToScreen(UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H) {
    if (*W == 0 || *H == 0 || gGuiWindowManager.ScreenWidth == 0 || gGuiWindowManager.ScreenHeight == 0) {
        *W = 0;
        return;
    }
    if (*X >= gGuiWindowManager.ScreenWidth || *Y >= gGuiWindowManager.ScreenHeight) {
        *W = 0;
        *H = 0;
        return;
    }
    if (*X + *W > gGuiWindowManager.ScreenWidth) {
        *W = gGuiWindowManager.ScreenWidth - *X;
    }
    if (*Y + *H > gGuiWindowManager.ScreenHeight) {
        *H = gGuiWindowManager.ScreenHeight - *Y;
    }
}

void GuiBackupFocusWindow(void) {
    if (gGuiWindowManager.FocusWin >= 0 && gGuiWindowManager.FocusWin < MAX_WINS && gGuiWindowManager.Windows[gGuiWindowManager.FocusWin].Active) {
        BackupWindowAt(gGuiWindowManager.FocusWin);
    }
}

