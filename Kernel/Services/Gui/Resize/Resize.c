/*
 * GuiResize.c — 右/底边与右下角拖拽改大小生命周期（PR-S3-guiresize-1）
 *
 * 线框/尺寸见 GuiResizeBand.c；热区见 GuiResizeHit.c。
 */
#include "GuiPrivate.h"
#include "UI.h"
#include "HalVideo.h"
#include "Desktop.h"

#define RESIZE_MIN_STEP 3u

void GuiResizeBegin(int Idx, UINT32 X, UINT32 Y) {
    int Edge;

    if (Idx < 0 || Idx >= MAX_WINS || !gGuiWindowManager.Windows[Idx].Active) {
        return;
    }
    Edge = GuiResizeEdgeAt(&gGuiWindowManager.Windows[Idx], X, Y);
    if (Edge == RESIZE_EDGE_NONE) {
        return;
    }
    gGuiWindowManager.DragWin = -1;
    gGuiWindowManager.DragArmed = 0;
    gGuiWindowManager.ResizeWin = Idx;
    gGuiWindowManager.ResizeEdge = Edge;
    gGuiWindowManager.ResizeOrigW = gGuiWindowManager.Windows[Idx].Width;
    gGuiWindowManager.ResizeOrigH = gGuiWindowManager.Windows[Idx].Height;
    gGuiWindowManager.ResizeAnchorX = (INT32)X;
    gGuiWindowManager.ResizeAnchorY = (INT32)Y;
    gGuiWindowManager.ResizeArmed = 1;
    gGuiWindowManager.ResizeBandOn = 0;
    gGuiWindowManager.ResizeBandW = 0;
    gGuiWindowManager.ResizeBandH = 0;
    gGuiWindowManager.CursorKind = GuiResizeCursorKindAt(X, Y);
}

void GuiResizeUpdate(UINT32 X, UINT32 Y) {
    UINT32 Nw;
    UINT32 Nh;
    UINT32 OdW;
    UINT32 OdH;

    if (gGuiWindowManager.ResizeWin < 0 || gGuiWindowManager.ResizeWin >= MAX_WINS || !gGuiWindowManager.Windows[gGuiWindowManager.ResizeWin].Active) {
        return;
    }
    ComputeResizeSize(gGuiWindowManager.ResizeWin, gGuiWindowManager.ResizeEdge, X, Y, &Nw, &Nh);
    OdW = (Nw > gGuiWindowManager.ResizeOrigW) ? (Nw - gGuiWindowManager.ResizeOrigW) : (gGuiWindowManager.ResizeOrigW - Nw);
    OdH = (Nh > gGuiWindowManager.ResizeOrigH) ? (Nh - gGuiWindowManager.ResizeOrigH) : (gGuiWindowManager.ResizeOrigH - Nh);
    if (gGuiWindowManager.ResizeArmed) {
        if (OdW < RESIZE_MIN_STEP && OdH < RESIZE_MIN_STEP) {
            return;
        }
        gGuiWindowManager.ResizeArmed = 0;
    }
    if (gGuiWindowManager.ResizeBandOn && Nw == gGuiWindowManager.ResizeBandW && Nh == gGuiWindowManager.ResizeBandH) {
        return;
    }

    ComposeBegin();
    GraphicsIrqEnter();
    CursorRestore();
    if (gGuiWindowManager.ResizeBandOn) {
        EraseResizeBand(gGuiWindowManager.ResizeWin, gGuiWindowManager.ResizeBandW, gGuiWindowManager.ResizeBandH);
    }
    DrawResizeBand(gGuiWindowManager.ResizeWin, Nw, Nh);
    CursorPaint();
    HalVideoPresent();
    GraphicsIrqLeave();
    ComposeEnd();
}

void GuiResizeEnd(void) {
    int Idx = gGuiWindowManager.ResizeWin;
    int Edge = gGuiWindowManager.ResizeEdge;
    UINT32 Nw;
    UINT32 Nh;
    int Did;

    if (Idx < 0 || Idx >= MAX_WINS || !gGuiWindowManager.Windows[Idx].Active) {
        gGuiWindowManager.ResizeWin = -1;
        gGuiWindowManager.ResizeArmed = 0;
        gGuiWindowManager.ResizeEdge = RESIZE_EDGE_NONE;
        gGuiWindowManager.ResizeBandOn = 0;
        return;
    }

    Did = gGuiWindowManager.ResizeBandOn ||
          (gGuiWindowManager.ResizeBandW != 0 && gGuiWindowManager.ResizeBandH != 0);
    /* 须在清 gGuiWindowManager.ResizeEdge / gGuiWindowManager.ResizeWin 之前算尺寸 */
    ComputeResizeSize(Idx, Edge, gGuiWindowManager.CursorX, gGuiWindowManager.CursorY, &Nw, &Nh);

    gGuiWindowManager.ResizeWin = -1;
    gGuiWindowManager.ResizeArmed = 0;
    gGuiWindowManager.ResizeEdge = RESIZE_EDGE_NONE;

    ComposeBegin();
    GraphicsIrqEnter();
    CursorRestore();
    if (gGuiWindowManager.ResizeBandOn) {
        EraseResizeBand(Idx, gGuiWindowManager.ResizeBandW, gGuiWindowManager.ResizeBandH);
    }

    if (Did && (Nw != gGuiWindowManager.Windows[Idx].Width || Nh != gGuiWindowManager.Windows[Idx].Height)) {
        UINT32 Ox = gGuiWindowManager.Windows[Idx].X;
        UINT32 Oy = gGuiWindowManager.Windows[Idx].Y;
        UINT32 Ow = gGuiWindowManager.Windows[Idx].Width;
        UINT32 Oh = gGuiWindowManager.Windows[Idx].Height;
        UINT32 Fx = Ox;
        UINT32 Fy = Oy;
        UINT32 Fw = Ow;
        UINT32 Fh = Oh;

        ExpandRectByWindowShadow(&Fx, &Fy, &Fw, &Fh);
        ClipRectToScreen(&Fx, &Fy, &Fw, &Fh);

        /*
         * 必须先改成新尺寸再 Clear：FillDesktopRectClipped 认
         * PointInAnyActiveWindow；若仍是旧 W/H，向左/上缩时露出条
         * 会被当成「仍在窗内」而跳过 → 旧客户区像素残留。
         */
        gGuiWindowManager.Windows[Idx].Width = Nw;
        gGuiWindowManager.Windows[Idx].Height = Nh;
        gGuiWindowManager.WinBackupValid[Idx] = 0;

        ClearOldDragFootprint(Fx, Fy, Fw, Fh, Idx);

        DrawWindowAt(Idx);
        RepaintAfterResize(Idx);
        BackupWindowAt(Idx);
        RaiseWindow(Idx);
        GuiFocusApply();
    }

    CursorPaint();
    HalVideoPresent();
    GraphicsIrqLeave();
    ComposeEnd();

    gGuiWindowManager.ResizeBandOn = 0;
    gGuiWindowManager.ResizeBandW = 0;
    gGuiWindowManager.ResizeBandH = 0;
}
