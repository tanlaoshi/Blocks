/*
 * GuiBackupSample.c — 备份像素采样与上层覆盖判断
 * 核心：GuiBackup.c
 */
#include "GuiPrivate.h"
#include "UI.h"
#include "HalVideo.h"
#include "Hal.h"
#include "PhysicalMemory.h"
#include "Theme.h"
#include "Font.h"
#include "Desktop.h"

/* 与 DrawWindowAt 布局一致；仅作无备份时的回退 */
UINT32 AnalyticWindowPixel(int Idx, UINT32 Px, UINT32 Py) {
    const GUI_WINDOW *W = &gGuiWindowManager.Windows[Idx];
    UINT32 Lx;
    UINT32 Ly;

    if (!W->Active) {
        return DesktopBgAt(Px, Py);
    }
    if (Px < W->X || Py < W->Y || Px >= W->X + W->Width || Py >= W->Y + W->Height) {
        return DesktopBgAt(Px, Py);
    }
    Lx = Px - W->X;
    Ly = Py - W->Y;
    if (Ly < TITLE_HEIGHT) {
        UINT32 Th = TITLE_HEIGHT;

        if (Th > W->Height) {
            Th = W->Height;
        }
        return TitleBarColorAtRow(Idx, Ly, Th);
    }
    if (Ly == W->Height - 1 || Lx == 0 || Lx == W->Width - 1) {
        return WindowBorderColor(Idx);
    }
    /* 与 DrawWindowAt 一致：白边内侧整片客户区底色 */
    return W->Background;
}

UINT32 SampleWindowBackupPixel(int Idx, UINT32 Px, UINT32 Py) {
    const GUI_WINDOW *W = &gGuiWindowManager.Windows[Idx];
    UINT32 Lx;
    UINT32 Ly;
    UINT32 Xp;
    UINT32 Yp;

    if (!gGuiWindowManager.WinBackupValid[Idx] || gGuiWindowManager.WinBackup[Idx] == 0 || !W->Active) {
        return DesktopBgAt(Px, Py);
    }
    if (Px < W->X || Py < W->Y) {
        return DesktopBgAt(Px, Py);
    }
    Lx = Px - W->X;
    Ly = Py - W->Y;
    if (Lx >= gGuiWindowManager.WinBackupW[Idx] || Ly >= gGuiWindowManager.WinBackupH[Idx]) {
        return DesktopBgAt(Px, Py);
    }
    /* PR-23d：缓冲是物理像素，采样点也要落到被采样逻辑格对应的物理格左上 */
    Xp = GuiPhysX(Px) - GuiPhysX(W->X);
    Yp = GuiPhysY(Py) - GuiPhysY(W->Y);
    if (Xp >= gGuiWindowManager.WinBackupWp[Idx] || Yp >= gGuiWindowManager.WinBackupHp[Idx]) {
        return DesktopBgAt(Px, Py);
    }
    return gGuiWindowManager.WinBackup[Idx][Yp * gGuiWindowManager.WinBackupWp[Idx] + Xp];
}


int WindowBackupCoversPixel(int Idx, UINT32 Px, UINT32 Py) {
    const GUI_WINDOW *W = &gGuiWindowManager.Windows[Idx];

    if (!gGuiWindowManager.WinBackupValid[Idx] || !W->Active) {
        return 0;
    }
    return Px >= W->X && Py >= W->Y &&
           Px < W->X + gGuiWindowManager.WinBackupW[Idx] && Py < W->Y + gGuiWindowManager.WinBackupH[Idx];
}


/*
 * 更高 z 是否盖住该像素。含 drop-shadow 右/下扩边：影落在下层窗体内时
 * 若只按窗 AABB 判断，Backup ReadPixel 会吸入阴影 → 松手烙印。
 */
int PixelCoveredByHigherWindow(int Idx, UINT32 Px, UINT32 Py) {
    int j;
    UINT32 N = ThemeWindowShadowSize();

    for (j = Idx + 1; j < MAX_WINS; j++) {
        const GUI_WINDOW *W = &gGuiWindowManager.Windows[j];
        UINT32 X1;
        UINT32 Y1;

        if (!W->Active) {
            continue;
        }
        if (PointInWindow(W, Px, Py)) {
            return 1;
        }
        if (N == 0) {
            continue;
        }
        X1 = W->X + W->Width + N;
        Y1 = W->Y + W->Height + N;
        if (Px >= W->X && Px < X1 && Py >= W->Y && Py < Y1) {
            return 1;
        }
    }
    return 0;
}

/* 体相交或上层阴影扩边扫过本窗 → 禁止 ForceFull ReadRect */
int WindowOccludedByOtherOrShadow(int Idx) {
    int j;
    UINT32 N = ThemeWindowShadowSize();

    if (WindowOccludedByOther(Idx)) {
        return 1;
    }
    if (N == 0 || Idx < 0 || Idx >= MAX_WINS || !gGuiWindowManager.Windows[Idx].Active) {
        return 0;
    }
    for (j = Idx + 1; j < MAX_WINS; j++) {
        if (!gGuiWindowManager.Windows[j].Active) {
            continue;
        }
        if (RectIntersects(gGuiWindowManager.Windows[Idx].X, gGuiWindowManager.Windows[Idx].Y,
                           gGuiWindowManager.Windows[Idx].Width, gGuiWindowManager.Windows[Idx].Height,
                           gGuiWindowManager.Windows[j].X, gGuiWindowManager.Windows[j].Y,
                           gGuiWindowManager.Windows[j].Width + N, gGuiWindowManager.Windows[j].Height + N)) {
            return 1;
        }
    }
    return 0;
}


void GuiBackupSyncRect(UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    const GUI_WINDOW *Win;
    UINT32 Row;
    UINT32 Col;
    UINT32 Bw;
    UINT32 Bh;
    UINT32 Px;
    UINT32 Py;
    UINT32 Bx;
    UINT32 By;
    UINT32 Ex;
    UINT32 Ey;
    UINT32 Ox;
    UINT32 Oy;
    int Idx;

    if (gGuiWindowManager.FocusWin < 0 || gGuiWindowManager.FocusWin >= MAX_WINS || !gGuiWindowManager.Windows[gGuiWindowManager.FocusWin].Active) {
        return;
    }
    Idx = gGuiWindowManager.FocusWin;
    if (!gGuiWindowManager.WinBackupValid[Idx] || gGuiWindowManager.WinBackup[Idx] == 0) {
        return;
    }
    Win = &gGuiWindowManager.Windows[Idx];
    Bw = gGuiWindowManager.WinBackupWp[Idx];
    Bh = gGuiWindowManager.WinBackupHp[Idx];
    if (Bw == 0 || Bh == 0) {
        return;
    }
    Ox = GuiPhysX(Win->X);
    Oy = GuiPhysY(Win->Y);
    for (Row = 0; Row < H; Row++) {
        Py = Y + Row;
        if (Py < Win->Y || Py >= Win->Y + gGuiWindowManager.WinBackupH[Idx]) {
            continue;
        }
        for (Col = 0; Col < W; Col++) {
            UINT32 Sy;

            Px = X + Col;
            if (Px < Win->X || Px >= Win->X + gGuiWindowManager.WinBackupW[Idx]) {
                continue;
            }
            if (PixelCoveredByHigherWindow(Idx, Px, Py)) {
                continue;
            }
            /*
             * PR-23d：一个逻辑格覆盖的整片物理像素都要同步，否则备份里
             * 这块还是旧的缩略采样，下一步贴回又把字边抹平。
             */
            Bx = GuiPhysX(Px) - Ox;
            By = GuiPhysY(Py) - Oy;
            Ex = GuiPhysX(Px + 1u) - Ox;
            Ey = GuiPhysY(Py + 1u) - Oy;
            if (Ex > Bw) {
                Ex = Bw;
            }
            if (Ey > Bh) {
                Ey = Bh;
            }
            for (Sy = By; Sy < Ey; Sy++) {
                UINT32 Sx;

                for (Sx = Bx; Sx < Ex; Sx++) {
                    gGuiWindowManager.WinBackup[Idx][Sy * Bw + Sx] =
                        HalVideoReadPixelPhys(GuiPhysX(Px) + (Sx - Bx),
                                              GuiPhysY(Py) + (Sy - By));
                }
            }
        }
    }
}
