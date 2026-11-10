/*
 * BackupPaint.c — PaintWindowFromBackup*（自 Backup.c 纯搬 · PR-NORM-16）
 */
#include "GuiPrivate.h"
#include "UI.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Theme.h"
#include "Font.h"
#include "Desktop.h"

void PaintWindowFromBackup(int Idx) {
    const GUI_WINDOW *Win = &gGuiWindowManager.Windows[Idx];

    if (!Win->Active) {
        return;
    }
    if (gGuiWindowManager.WinBackupValid[Idx] && gGuiWindowManager.WinBackup[Idx] != 0) {
        /* PR-23d：逐物理像素贴回，150% 下与抓的时候像素一致（不铺块） */
        HalVideoWriteRectPhys(GuiPhysicalX(Win->X), GuiPhysicalY(Win->Y),
                              gGuiWindowManager.WinBackupWp[Idx],
                              gGuiWindowManager.WinBackupHp[Idx],
                              gGuiWindowManager.WinBackup[Idx]);
        DrawWindowChromeAt(Idx);
        /* 阴影由 SyncWindowVisualsEx 第二遍统一画，避免先画影再被下层 WriteRect 打乱 */
        return;
    }
    DrawWindowAt(Idx);
}

void PaintWindowFromBackupClipped(int Idx, UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    const GUI_WINDOW *Win = &gGuiWindowManager.Windows[Idx];
    UINT32 Bw;
    UINT32 Bh;
    UINT32 Ix;
    UINT32 Iy;
    UINT32 Iw;
    UINT32 Ih;
    UINT32 Right;
    UINT32 Bottom;
    UINT32 Row;
    UINT32 Cxp;
    UINT32 Cyp;
    UINT32 Cwp;
    UINT32 Chp;
    UINT32 Bx;
    UINT32 By;

    if (!Win->Active || W == 0 || H == 0) {
        return;
    }
    if (!gGuiWindowManager.WinBackupValid[Idx] || gGuiWindowManager.WinBackup[Idx] == 0) {
        DrawWindowAt(Idx);
        return;
    }
    Bw = gGuiWindowManager.WinBackupWp[Idx];
    Bh = gGuiWindowManager.WinBackupHp[Idx];
    if (Bw == 0 || Bh == 0) {
        return;
    }
    Ix = Win->X > X ? Win->X : X;
    Iy = Win->Y > Y ? Win->Y : Y;
    /* 逻辑裁剪框按备份自身的逻辑范围收（Bw/Bh 已是物理，回算逻辑） */
    Right = Win->X + gGuiWindowManager.WinBackupW[Idx];
    if (X + W < Right) {
        Right = X + W;
    }
    Bottom = Win->Y + gGuiWindowManager.WinBackupH[Idx];
    if (Y + H < Bottom) {
        Bottom = Y + H;
    }
    if (Right <= Ix || Bottom <= Iy) {
        return;
    }
    Iw = Right - Ix;
    Ih = Bottom - Iy;
    /* 裁剪框换物理，再逐物理行贴回（源偏移按窗原点算） */
    GuiPhysicalRect(Ix, Iy, Iw, Ih, &Cxp, &Cyp, &Cwp, &Chp);
    Bx = GuiPhysicalX(Ix) - GuiPhysicalX(Win->X);
    By = GuiPhysicalY(Iy) - GuiPhysicalY(Win->Y);
    if (Bx >= Bw || By >= Bh) {
        return;
    }
    if (Bx + Cwp > Bw) {
        Cwp = Bw - Bx;
    }
    if (By + Chp > Bh) {
        Chp = Bh - By;
    }
    for (Row = 0; Row < Chp; Row++) {
        const UINT32 *Src = &gGuiWindowManager.WinBackup[Idx][(By + Row) * Bw + Bx];

        HalVideoWriteRectPhys(Cxp, Cyp + Row, Cwp, 1, Src);
    }
    /* 标题/边框按遮挡重画：不会写到更高 z 的像素上 */
    DrawWindowChromeAt(Idx);
}

