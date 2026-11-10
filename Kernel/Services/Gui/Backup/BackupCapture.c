/*
 * BackupCapture.c — BackupWindowAtEx / 可见像素抓取（自 Backup.c 纯搬 · PR-NORM-16）
 */
#include "GuiPrivate.h"
#include "UI.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Theme.h"
#include "Font.h"
#include "Desktop.h"

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
            UINT32 Bx = GuiPhysicalX(Px) - Xp;
            UINT32 By = GuiPhysicalY(Py) - Yp;
            UINT32 Ex = GuiPhysicalX(Px + 1u) - Xp;
            UINT32 Ey = GuiPhysicalY(Py + 1u) - Yp;
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

    GuiPhysicalRect(Win->X, Win->Y, Rw, Rh, &Xp, &Yp, &Wp, &Hp);
    HadValid = gGuiWindowManager.WinBackupValid[Idx];
    OldBuf = gGuiWindowManager.WinBackup[Idx];
    OldWp = gGuiWindowManager.WinBackupWp[Idx];
    OldHp = gGuiWindowManager.WinBackupHp[Idx];
    if (!EnsureWindowBackupBuffer(Idx)) {
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

