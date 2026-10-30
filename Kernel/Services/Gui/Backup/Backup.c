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
    UINT32 Wp;
    UINT32 Hp;

    if (Idx < 0 || Idx >= MAX_WINS || !gGuiWindowManager.Windows[Idx].Active) {
        return 0;
    }
    if (gGuiWindowManager.Windows[Idx].Width == 0 || gGuiWindowManager.Windows[Idx].Height == 0) {
        return 0;
    }
    /*
     * PR-23d：缓冲按物理像素开（150% 下 1.5×1.5 = 2.25 倍像素），否则
     * 整窗只能存一份「每个逻辑格一个采样点」的缩略图，贴回时文字必糊。
     */
    GuiPhysRect(gGuiWindowManager.Windows[Idx].X, gGuiWindowManager.Windows[Idx].Y,
                gGuiWindowManager.Windows[Idx].Width,
                gGuiWindowManager.Windows[Idx].Height, 0, 0, &Wp, &Hp);
    Pages = BackupPageCount(Wp, Hp);
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
 * PR-23d：被挡窗只更新可见像素。按逻辑像素判遮挡，但写入的是该格覆盖的
 * 整片物理像素（从帧缓冲逐物理像素取），被挡处保留旧备份的同一片物理像素。
 * 这样可见区的字形细节不会被「每格一个采样点」抹平。
 */
static void BackupVisiblePixels(int Idx, UINT32 Rw, UINT32 Rh, UINT32 Xp,
                                UINT32 Yp, UINT32 Wp, UINT32 Hp, UINT32 *OldBuf,
                                UINT32 OldWp, UINT32 OldHp) {
    const GUI_WINDOW *Win = &gGuiWindowManager.Windows[Idx];
    UINT32 *Buf = gGuiWindowManager.WinBackup[Idx];
    UINT32 Row;
    UINT32 Col;

    for (Row = 0; Row < Rh; Row++) {
        UINT32 Py = Win->Y + Row;

        for (Col = 0; Col < Rw; Col++) {
            UINT32 Px = Win->X + Col;
            UINT32 Bx = GuiPhysX(Px) - Xp;
            UINT32 By = GuiPhysY(Py) - Yp;
            UINT32 Ex = GuiPhysX(Px + 1u) - Xp;
            UINT32 Ey = GuiPhysY(Py + 1u) - Yp;
            int Covered = PixelCoveredByHigherWindow(Idx, Px, Py);
            UINT32 Sy;

            if (Ex > Wp) {
                Ex = Wp;
            }
            if (Ey > Hp) {
                Ey = Hp;
            }
            for (Sy = By; Sy < Ey; Sy++) {
                UINT32 Sx;

                for (Sx = Bx; Sx < Ex; Sx++) {
                    UINT32 *Dst = &Buf[Sy * Wp + Sx];

                    if (Covered) {
                        if (OldBuf != Buf && Sx < OldWp && Sy < OldHp) {
                            *Dst = OldBuf[Sy * OldWp + Sx];
                        }
                        continue;
                    }
                    *Dst = HalVideoReadPixelPhys(Xp + Sx, Yp + Sy);
                }
            }
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
    UINT32 Xp;
    UINT32 Yp;
    UINT32 Wp;
    UINT32 Hp;
    int HadValid;
    UINT32 *OldBuf;
    UINT32 OldWp;
    UINT32 OldHp;
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

    GuiPhysRect(Win->X, Win->Y, Rw, Rh, &Xp, &Yp, &Wp, &Hp);
    HadValid = gGuiWindowManager.WinBackupValid[Idx];
    OldBuf = gGuiWindowManager.WinBackup[Idx];
    OldWp = gGuiWindowManager.WinBackupWp[Idx];
    OldHp = gGuiWindowManager.WinBackupHp[Idx];
    if (!EnsureWindowBackupBuf(Idx)) {
        gGuiWindowManager.WinBackupValid[Idx] = 0;
        return;
    }
    if (gGuiWindowManager.WinBackup[Idx] != OldBuf) {
        HadValid = 0;
    }

    WasVisible = gGuiWindowManager.CursorVisible;
    ComposeBegin();
    GraphicsIrqEnter();
    CursorRestore();
    GraphicsIrqLeave();

    /*
     * ForceFull：compose 自下而上、上层尚未画时整窗 ReadRect（含将被盖住的区域）。
     * 非 ForceFull 且被挡（含上层阴影扫过）：只更新可见像素，被挡处保留旧备份。
     */
    if (!ForceFull && WindowOccludedByOtherOrShadow(Idx)) {
        if (!HadValid) {
            gGuiWindowManager.WinBackupValid[Idx] = 0;
            if (WasVisible) {
                GraphicsIrqEnter();
                CursorPaint();
                GraphicsIrqLeave();
            }
            ComposeEnd();
            return;
        }
        BackupVisiblePixels(Idx, Rw, Rh, Xp, Yp, Wp, Hp, OldBuf, OldWp, OldHp);
        gGuiWindowManager.WinBackupW[Idx] = Rw;
        gGuiWindowManager.WinBackupH[Idx] = Rh;
        gGuiWindowManager.WinBackupWp[Idx] = Wp;
        gGuiWindowManager.WinBackupHp[Idx] = Hp;
        gGuiWindowManager.WinBackupValid[Idx] = 1;
        if (WasVisible) {
            GraphicsIrqEnter();
            CursorPaint();
            GraphicsIrqLeave();
        }
        ComposeEnd();
        return;
    }

    /* PR-23d：整窗按物理像素抓（1:1，不留缩略图） */
    HalVideoReadRectPhys(Xp, Yp, Wp, Hp, gGuiWindowManager.WinBackup[Idx]);
    gGuiWindowManager.WinBackupW[Idx] = Rw;
    gGuiWindowManager.WinBackupH[Idx] = Rh;
    gGuiWindowManager.WinBackupWp[Idx] = Wp;
    gGuiWindowManager.WinBackupHp[Idx] = Hp;
    gGuiWindowManager.WinBackupValid[Idx] = 1;
    if (WasVisible) {
        GraphicsIrqEnter();
        CursorPaint();
        GraphicsIrqLeave();
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
        /* PR-23d：逐物理像素贴回，150% 下与抓的时候像素一致（不铺块） */
        HalVideoWriteRectPhys(GuiPhysX(Win->X), GuiPhysY(Win->Y),
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
    GuiPhysRect(Ix, Iy, Iw, Ih, &Cxp, &Cyp, &Cwp, &Chp);
    Bx = GuiPhysX(Ix) - GuiPhysX(Win->X);
    By = GuiPhysY(Iy) - GuiPhysY(Win->Y);
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


void ShiftWinBackupsUp(int From, int To) {
    UINT32 *Buf = gGuiWindowManager.WinBackup[From];
    UINT32 Bw = gGuiWindowManager.WinBackupW[From];
    UINT32 Bh = gGuiWindowManager.WinBackupH[From];
    UINT32 Bwp = gGuiWindowManager.WinBackupWp[From];
    UINT32 Bhp = gGuiWindowManager.WinBackupHp[From];
    UINT32 Bp = gGuiWindowManager.WinBackupPages[From];
    int Bv = gGuiWindowManager.WinBackupValid[From];
    int J;

    for (J = From; J < To; J++) {
        gGuiWindowManager.WinBackup[J] = gGuiWindowManager.WinBackup[J + 1];
        gGuiWindowManager.WinBackupW[J] = gGuiWindowManager.WinBackupW[J + 1];
        gGuiWindowManager.WinBackupH[J] = gGuiWindowManager.WinBackupH[J + 1];
        gGuiWindowManager.WinBackupWp[J] = gGuiWindowManager.WinBackupWp[J + 1];
        gGuiWindowManager.WinBackupHp[J] = gGuiWindowManager.WinBackupHp[J + 1];
        gGuiWindowManager.WinBackupPages[J] = gGuiWindowManager.WinBackupPages[J + 1];
        gGuiWindowManager.WinBackupValid[J] = gGuiWindowManager.WinBackupValid[J + 1];
    }
    gGuiWindowManager.WinBackup[To] = Buf;
    gGuiWindowManager.WinBackupW[To] = Bw;
    gGuiWindowManager.WinBackupH[To] = Bh;
    gGuiWindowManager.WinBackupWp[To] = Bwp;
    gGuiWindowManager.WinBackupHp[To] = Bhp;
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

