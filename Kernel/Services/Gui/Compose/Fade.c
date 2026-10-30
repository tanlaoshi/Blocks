/*
 * GuiFade.c — PR-GUI-l3-fade：开关窗淡入淡出中间帧
 *
 * 用窗备份与「无本窗」桌面合成层做 Src-over 插值；fade=0 跳过。
 * 不扩阴影（影在首/末帧随 Sync/Draw 出现），避免拖动路径复杂度。
 */
#include "GuiPrivate.h"
#include "HalVideo.h"
#include "Hal.h"
#include "PhysicalMemory.h"
#include "Theme.h"
#include "Desktop.h"

static void FadeFrameDelay(void) {
    UINTN N = HalCpuIsHypervisor() ? 40000u : 180000u;
    UINTN i;

    for (i = 0; i < N; i++) {
        HalCpuRelax();
    }
}

/*
 * 不 Present：铺桌面 + 其它窗（含影），读本窗矩形为 under。
 * 调用方须已 ComposeBegin + 擦光标。
 * 勿把本窗 Active 清零：淡出期间其它核上的 poll_input 会误得 -1（USER poll fail）。
 */
static int CaptureUnderNoPresent(int Idx, UINT32 *Under, UINT32 Rw, UINT32 Rh,
                                 UINT32 Wp, UINT32 Hp) {
    const GUI_WINDOW *Win = &gGuiWindowManager.Windows[Idx];
    int i;

    HalVideoClearClip();
    DesktopFillRect(Win->X, Win->Y, Rw, Rh);
    DesktopDrawRect(Win->X, Win->Y, Rw, Rh);
    for (i = 0; i < MAX_WINS; i++) {
        if (i == Idx || !gGuiWindowManager.Windows[i].Active) {
            continue;
        }
        if (!RectIntersects(gGuiWindowManager.Windows[i].X, gGuiWindowManager.Windows[i].Y, gGuiWindowManager.Windows[i].Width,
                            gGuiWindowManager.Windows[i].Height, Win->X, Win->Y, Rw, Rh)) {
            continue;
        }
        if (gGuiWindowManager.WinBackupValid[i] && gGuiWindowManager.WinBackup[i] != 0) {
            PaintWindowFromBackup(i);
        } else {
            DrawWindowAtEx(i, WindowOccludedByOther(i) ? 1 : 0);
        }
    }
    for (i = 0; i < MAX_WINS; i++) {
        if (i == Idx || !gGuiWindowManager.Windows[i].Active) {
            continue;
        }
        if (!RectIntersects(gGuiWindowManager.Windows[i].X, gGuiWindowManager.Windows[i].Y, gGuiWindowManager.Windows[i].Width,
                            gGuiWindowManager.Windows[i].Height, Win->X, Win->Y, Rw, Rh)) {
            continue;
        }
        DrawWindowShadowAt(i);
    }
    /*
     * PR-23d：under 也按物理像素存，否则淡入淡出中间帧整窗糊。
     * 尺寸取窗备份的物理宽高——BlendFrame 把 Under 与备份当同 stride 的
     * 平铺图逐像素混，两者必须严格同尺寸。
     */
    HalVideoReadRectPhys(GuiPhysX(Win->X), GuiPhysY(Win->Y), Wp, Hp, Under);
    return 1;
}

static void BlendFrame(UINT32 *Dst, const UINT32 *Under, const UINT32 *Over,
                       UINT32 N, UINT8 Alpha) {
    UINT32 i;

    if (Alpha == 0) {
        for (i = 0; i < N; i++) {
            Dst[i] = Under[i];
        }
        return;
    }
    if (Alpha == 255) {
        for (i = 0; i < N; i++) {
            Dst[i] = Over[i];
        }
        return;
    }
    for (i = 0; i < N; i++) {
        Dst[i] = HalVideoBlendRgb(Under[i], Over[i], Alpha);
    }
}

/*
 * FadeIn=1：备份已含不透明窗体，从透到实。
 * FadeIn=0：关窗前调用，窗仍 Active；结束后 FB 为本窗矩形 under（调用方再收尾）。
 */
void GuiAnimateWindowFade(int Idx, int FadeIn) {
    const GUI_WINDOW *Win;
    UINT32 Steps;
    UINT32 Rw;
    UINT32 Rh;
    UINT32 Xp;
    UINT32 Yp;
    UINT32 Wp;
    UINT32 Hp;
    UINT32 Pages;
    UINT32 *Under = 0;
    UINT32 *Frame = 0;
    UINT32 Npix;
    UINT32 Step;
    UINT8 Alpha;
    int Ok = 0;

    Steps = ThemeWindowFadeSteps();
    if (!ThemeIsFadeEnabled() || Steps == 0 || Idx < 0 || Idx >= MAX_WINS) {
        return;
    }
    Win = &gGuiWindowManager.Windows[Idx];
    if (!Win->Active || Win->Width == 0 || Win->Height == 0) {
        return;
    }
    if (!gGuiWindowManager.WinBackupValid[Idx] || gGuiWindowManager.WinBackup[Idx] == 0) {
        BackupWindowAt(Idx);
        if (!gGuiWindowManager.WinBackupValid[Idx] || gGuiWindowManager.WinBackup[Idx] == 0) {
            return;
        }
    }

    Rw = gGuiWindowManager.WinBackupW[Idx];
    Rh = gGuiWindowManager.WinBackupH[Idx];
    if (Rw == 0 || Rh == 0) {
        return;
    }
    if (Win->X + Rw > gGuiWindowManager.ScreenWidth) {
        Rw = gGuiWindowManager.ScreenWidth - Win->X;
    }
    if (Win->Y + Rh > gGuiWindowManager.ScreenHeight) {
        Rh = gGuiWindowManager.ScreenHeight - Win->Y;
    }
    if (Rw == 0 || Rh == 0) {
        return;
    }

    /* PR-23d：缓冲按物理像素开；Under/Frame 与窗备份同尺寸同 stride */
    Xp = GuiPhysX(Win->X);
    Yp = GuiPhysY(Win->Y);
    Wp = gGuiWindowManager.WinBackupWp[Idx];
    Hp = gGuiWindowManager.WinBackupHp[Idx];
    if (Wp == 0 || Hp == 0) {
        return;
    }
    Pages = BackupPageCount(Wp, Hp);
    Under = (UINT32 *)PhysicalMemoryAllocatePages(Pages);
    Frame = (UINT32 *)PhysicalMemoryAllocatePages(Pages);
    if (Under == 0 || Frame == 0) {
        goto done;
    }

    ComposeBegin();
    GraphicsIrqEnter();
    CursorRestore();
    GraphicsIrqLeave();

    if (!CaptureUnderNoPresent(Idx, Under, Rw, Rh, Wp, Hp)) {
        ComposeEnd();
        goto done;
    }

    Npix = Wp * Hp;
    if (FadeIn) {
        for (Step = 1; Step <= Steps; Step++) {
            Alpha = (UINT8)((Step * 255u) / Steps);
            BlendFrame(Frame, Under, gGuiWindowManager.WinBackup[Idx], Npix, Alpha);
            HalVideoWriteRectPhys(Xp, Yp, Wp, Hp, Frame);
            GraphicsIrqEnter();
            HalVideoPresent();
            GraphicsIrqLeave();
            if (Step < Steps) {
                FadeFrameDelay();
            }
        }
        HalVideoWriteRectPhys(Xp, Yp, Wp, Hp, gGuiWindowManager.WinBackup[Idx]);
        DrawWindowChromeAt(Idx);
        DrawWindowShadowAt(Idx);
    } else {
        for (Step = Steps; Step >= 1; Step--) {
            Alpha = (UINT8)((Step * 255u) / Steps);
            BlendFrame(Frame, Under, gGuiWindowManager.WinBackup[Idx], Npix, Alpha);
            HalVideoWriteRectPhys(Xp, Yp, Wp, Hp, Frame);
            GraphicsIrqEnter();
            HalVideoPresent();
            GraphicsIrqLeave();
            if (Step > 1) {
                FadeFrameDelay();
            }
        }
        HalVideoWriteRectPhys(Xp, Yp, Wp, Hp, Under);
    }

    GraphicsIrqEnter();
    CursorPaint();
    HalVideoPresent();
    GraphicsIrqLeave();
    ComposeEnd();
    Ok = 1;

done:
    (void)Ok;
    if (Under != 0) {
        PhysicalMemoryFreePages(Under, Pages);
    }
    if (Frame != 0) {
        PhysicalMemoryFreePages(Frame, Pages);
    }
}
