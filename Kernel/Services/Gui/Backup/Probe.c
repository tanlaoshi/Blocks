/*
 * Probe.c — PR-23d 自测：窗备份「抓 → 贴」往返的物理像素一致性
 *
 * 判据（数字，不靠肉眼）：把焦点窗整窗抓进备份，再从备份贴回，逐物理像素
 * 与抓之前比较。diff=0 才说明备份/还原没有把细节抹平（150% 下不糊）。
 * 旧实现按逻辑像素存：150% 时每格只存 1 个采样点，贴回时铺成 1~2px 块，
 * 文字边缘立刻碎成方块 —— 于是 diff 上千。
 */
#include "GuiPrivate.h"
#include "UI.h"
#include "HalVideo.h"
#include "PhysicalMemory.h"
#include "Hal.h"

/* 客户区外留一圈边框：它由 DrawWindowChromeAt 现画，不属于备份保真 */
#define GUI_PROBE_INSET 1u
#define GUI_PROBE_MIN_SIDE 2u

/*
 * 只测客户区：标题/边框由 DrawWindowChromeAt 现画（含焦点色），不属于
 * 「备份保真」；把它算进来会引入与本次改动无关的差异。
 */
static void ProbeContentRect(const GUI_WINDOW *W, UINT32 *X, UINT32 *Y,
                             UINT32 *Ww, UINT32 *Hh) {
    UINT32 Left = W->X;
    UINT32 Top = W->Y;
    UINT32 Width = W->Width;
    UINT32 Height = W->Height;

    if (Width > GUI_PROBE_MIN_SIDE) {
        Left += GUI_PROBE_INSET;
        Width -= 2u * GUI_PROBE_INSET;
    }
    if (Height > (TITLE_HEIGHT + GUI_PROBE_INSET)) {
        Top += TITLE_HEIGHT;
        Height -= TITLE_HEIGHT + GUI_PROBE_INSET;
    }
    if (Left + Width > gGuiWindowManager.ScreenWidth) {
        Width = (Left < gGuiWindowManager.ScreenWidth)
                    ? (gGuiWindowManager.ScreenWidth - Left)
                    : 0u;
    }
    if (Top + Height > gGuiWindowManager.ScreenHeight) {
        Height = (Top < gGuiWindowManager.ScreenHeight)
                     ? (gGuiWindowManager.ScreenHeight - Top)
                     : 0u;
    }
    *X = Left;
    *Y = Top;
    *Ww = Width;
    *Hh = Height;
}

static int ProbeCompare(const UINT32 *Before, UINT32 Xp, UINT32 Yp,
                        UINT32 Wp, UINT32 Hp, UINT32 *OutDiff,
                        UINT32 *OutRows) {
    UINT32 Pages = BackupPageCount(Wp, 1u);
    UINT32 *RowBuf;
    UINT32 Row;
    UINT32 Diff = 0;
    UINT32 Rows = 0;

    if (Pages == 0u) {
        return -1;
    }
    RowBuf = (UINT32 *)PhysicalMemoryAllocatePages(Pages);
    if (RowBuf == 0) {
        return -1;
    }
    for (Row = 0; Row < Hp; Row++) {
        UINT32 Col;
        UINT32 RowDiff = 0;

        HalVideoReadRectPhys(Xp, Yp + Row, Wp, 1u, RowBuf);
        for (Col = 0; Col < Wp; Col++) {
            if (RowBuf[Col] != Before[Row * Wp + Col]) {
                RowDiff++;
            }
        }
        if (RowDiff != 0u) {
            Rows++;
        }
        Diff += RowDiff;
    }
    PhysicalMemoryFreePages(RowBuf, Pages);
    *OutDiff = Diff;
    *OutRows = Rows;
    return 0;
}

int GuiBackupRoundTripDiff(UINT32 *OutWp, UINT32 *OutHp, UINT32 *OutDiff,
                           UINT32 *OutRows) {
    int Idx = gGuiWindowManager.FocusWin;
    const GUI_WINDOW *Win;
    UINT32 Lx;
    UINT32 Ly;
    UINT32 Lw;
    UINT32 Lh;
    UINT32 Xp;
    UINT32 Yp;
    UINT32 Wp;
    UINT32 Hp;
    UINT32 Pages;
    UINT32 *Before;
    int WasVisible;
    int Rc = -1;

    if (Idx < 0 || Idx >= MAX_WINS || !gGuiWindowManager.Windows[Idx].Active) {
        return -1;
    }
    Win = &gGuiWindowManager.Windows[Idx];
    ProbeContentRect(Win, &Lx, &Ly, &Lw, &Lh);
    if (Lw == 0u || Lh == 0u) {
        return -1;
    }
    GuiPhysRect(Lx, Ly, Lw, Lh, &Xp, &Yp, &Wp, &Hp);
    if (Wp == 0u || Hp == 0u) {
        return -1;
    }
    Pages = BackupPageCount(Wp, Hp);
    if (Pages == 0u) {
        return -1;
    }
    Before = (UINT32 *)PhysicalMemoryAllocatePages(Pages);
    if (Before == 0) {
        return -1;
    }
    /* 光标要藏起来：它不在窗备份里，留在屏上会被算成全屏差异 */
    ComposeBegin();
    WasVisible = gGuiWindowManager.CursorVisible;
    if (WasVisible) {
        GraphicsIrqEnter();
        CursorRestore();
        GraphicsIrqLeave();
    }
    BackupWindowAt(Idx);
    HalVideoReadRectPhys(Xp, Yp, Wp, Hp, Before);
    PaintWindowFromBackup(Idx);
    if (ProbeCompare(Before, Xp, Yp, Wp, Hp, OutDiff, OutRows) == 0) {
        *OutWp = Wp;
        *OutHp = Hp;
        Rc = 0;
    }
    if (WasVisible) {
        GraphicsIrqEnter();
        CursorPaint();
        GraphicsIrqLeave();
    }
    HalVideoPresentFlush();
    ComposeEnd();
    PhysicalMemoryFreePages(Before, Pages);
    return Rc;
}
