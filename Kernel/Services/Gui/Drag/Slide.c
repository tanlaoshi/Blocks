/*
 * Slide.c — 拖帧门闩 + CopyRect 平移（gui-g8-drag）
 *
 * gDragFrame：一帧只一核写后缓冲并 Present；他核见忙 → HoldDrain。
 * GuiPresentBlocked = gDragFrame | ComposeBusy（G1 门闩表）。
 * GuiDragActive = DragWin|ResizeWin ≥0 — Damage 早退用这个，≠ gDragFrame。
 *
 * RedrawDragFrameSlide（Move 有备份时热路径）：
 *   CopyRect 整窗 → 只擦露出条（勿 Expand 影吃进新窗边）→ 备份重贴
 *   → 旧∪新 MarkDirty → PresentFlush（抬高条带）→ 复原 ChunkRows
 * 与 Composite.RedrawDragFrame（擦足迹+贴备份+Present，非 CopyRect）对照；
 * 改拖闪先认清走的是哪条，勿两刀同时拧。
 */
#include "GuiPrivate.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Theme.h"
#include "Debug.h"

/* 一帧只许一个核改后缓冲并 Present；另一核见忙则排空鼠标 */
static volatile UINT32 gDragFrame;
static UINT32 gDragFrames;

int GuiDragFrameTry(void) {
    return __sync_lock_test_and_set(&gDragFrame, 1u) == 0;
}

void GuiDragFrameLeave(void) {
    __sync_lock_release(&gDragFrame);
}

int GuiPresentBlocked(void) {
    return gDragFrame != 0 || gGuiWindowManager.ComposeBusy != 0;
}

int GuiDragActive(void) {
    return gGuiWindowManager.DragWin >= 0 || gGuiWindowManager.ResizeWin >= 0;
}

void GuiDragFrameAccount(void) {
    gDragFrames++;
}

void GuiDragFrameLog(void) {
    if (gDragFrames == 0) {
        return;
    }
    DebugWrite("Gui: drag frames ");
    DebugHex32(gDragFrames);
    DebugWrite("\n");
    gDragFrames = 0;
}

/*
 * 露出条勿 ExpandRectByWindowShadow：影向右下扩，会吃进已 CopyRect 的新窗左边 → 花块。
 * 旧影由随后 DrawWindowShadowAt / 桌面擦条覆盖。
 */
/* 底边阴影在窗体之下。水平移动时清掉旧影，不会吃进已经 CopyRect 的新窗。 */
static void ClearOldBottomShadow(UINT32 OldX, UINT32 OldY, UINT32 Nx, UINT32 Ny,
                                 UINT32 Ww, UINT32 Wh, int DragIdx) {
    UINT32 N;
    UINT32 X;
    UINT32 Right;
    UINT32 Y;

    if (!ThemeIsShadowEnabled()) {
        return;
    }
    N = ThemeWindowShadowSize();
    if (N == 0) {
        return;
    }
    X = (OldX < Nx) ? OldX : Nx;
    Right = (OldX + Ww > Nx + Ww) ? (OldX + Ww) : (Nx + Ww);
    Right += N;
    Y = (OldY + Wh > Ny + Wh) ? (OldY + Wh) : (Ny + Wh);
    if (Right <= X) {
        return;
    }
    ClearOldDragFootprint(X, Y, Right - X, N, DragIdx);
}

static void ClearExposedStrip(UINT32 X, UINT32 Y, UINT32 W, UINT32 H, int DragIdx) {
    UINT32 Fx = X;
    UINT32 Fy = Y;
    UINT32 Fw = W;
    UINT32 Fh = H;

    if (W == 0 || H == 0) {
        return;
    }
    ClipRectToScreen(&Fx, &Fy, &Fw, &Fh);
    ClearOldDragFootprint(Fx, Fy, Fw, Fh, DragIdx);
}

void RedrawDragFrameSlide(int DragIdx, UINT32 OldX, UINT32 OldY) {
    GUI_WINDOW *Drag;
    UINT32 Nx;
    UINT32 Ny;
    UINT32 Ww;
    UINT32 Wh;
    INT32 Dx;
    INT32 Dy;

    if (DragIdx < 0 || DragIdx >= MAX_WINS) {
        return;
    }
    Drag = &gGuiWindowManager.Windows[DragIdx];
    Nx = Drag->X;
    Ny = Drag->Y;
    Ww = Drag->Width;
    Wh = Drag->Height;
    Dx = (INT32)Nx - (INT32)OldX;
    Dy = (INT32)Ny - (INT32)OldY;

    HalVideoClearClip();
    HalVideoCopyRect(OldX, OldY, Nx, Ny, Ww, Wh);

    if (Dx > 0) {
        ClearExposedStrip(OldX, OldY, (UINT32)Dx, Wh, DragIdx);
    } else if (Dx < 0) {
        ClearExposedStrip(Nx + Ww, OldY, (UINT32)(-Dx), Wh, DragIdx);
    }
    if (Dy > 0) {
        ClearExposedStrip(OldX, OldY, Ww, (UINT32)Dy, DragIdx);
    } else if (Dy < 0) {
        ClearExposedStrip(OldX, Ny + Wh, Ww, (UINT32)(-Dy), DragIdx);
    }
    ClearOldBottomShadow(OldX, OldY, Nx, Ny, Ww, Wh, DragIdx);

    /* Copy/擦条偶发踩窗边：用备份重贴保证客户区干净。
     * 拖动中不画本窗阴影：底边阴影按像素画，翻页时左下角右下角会一小块一小块补上。
     * 松手后 SyncWindowVisuals 再画。 */
    if (gGuiWindowManager.WinBackupValid[DragIdx] && gGuiWindowManager.WinBackup[DragIdx] != 0) {
        PaintWindowFromBackup(DragIdx);
    }

    /*
     * igpu-5：旧∪新（含右下影）并成一块脏区再 Present。
     * GPU 路径一次 SRC_COPY；CPU 回退时抬高条带，减轻左右细闪。
     */
    {
        UINT32 Ux = (OldX < Nx) ? OldX : Nx;
        UINT32 Uy = (OldY < Ny) ? OldY : Ny;
        UINT32 Ur = (OldX + Ww > Nx + Ww) ? (OldX + Ww) : (Nx + Ww);
        UINT32 Ub = (OldY + Wh > Ny + Wh) ? (OldY + Wh) : (Ny + Wh);
        UINT32 Shadow = ThemeWindowShadowSize();

        if (Ur + Shadow < gGuiWindowManager.ScreenWidth) {
            Ur += Shadow;
        } else {
            Ur = gGuiWindowManager.ScreenWidth;
        }
        if (Ub + Shadow < gGuiWindowManager.ScreenHeight) {
            Ub += Shadow;
        } else {
            Ub = gGuiWindowManager.ScreenHeight;
        }
        if (Ur > Ux && Ub > Uy) {
            HalVideoMarkDirty(Ux, Uy, Ur - Ux, Ub - Uy);
        }
        /* 整块 Present：GPU 双页 blit / 翻页；CPU 抬高条带。Flush 把 Partial 续完。 */
        HalVideoSetPresentChunkRows(16384u);
    }
    GuiDragFrameAccount();
    HalVideoPresentFlush();
    HalVideoSetPresentChunkRows(0);
}
