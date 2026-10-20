/*
 * GuiResizeHit.c — 改大小热区命中与光标外形（PR-S3-guiresize-1）
 */
#include "GuiPrivate.h"
#include "Desktop.h"

#define RESIZE_HOT 16u

int GuiResizeEdgeAt(const GUI_WINDOW *W, UINT32 X, UINT32 Y) {
    UINT32 Rx;
    UINT32 Ry;
    int OnRight;
    int OnBottom;
    int Idx;

    if (!W || !W->Active || W->FixedSize ||
        W->Width < RESIZE_HOT || W->Height < RESIZE_HOT) {
        return RESIZE_EDGE_NONE;
    }
    if (X < W->X || Y < W->Y || X >= W->X + W->Width || Y >= W->Y + W->Height) {
        return RESIZE_EDGE_NONE;
    }
    /*
     * USER 底栏：按钮矩形 + 其下垫/边框仍落在 RESIZE_HOT(16) 内。
     * 只排除 UserButtonHit 时，点在钮缝/钮下会抢成 RESIZE_S → 拖一下窗「向下延伸」
     *（GuiDemo 来回点 OK/Cancel 即现）。整条底栏带禁南向改大小。
     */
    Idx = (int)(W - gGuiWindowManager.Windows);
    if (Idx >= 0 && Idx < MAX_WINS && W == &gGuiWindowManager.Windows[Idx] &&
        W->Kind == GUI_WIN_USER) {
        int Bi;
        int HasBtn = 0;

        for (Bi = 0; Bi < 4; Bi++) {
            if (gGuiWindowManager.UserSlot[Idx].UserButtonUsed[Bi]) {
                HasBtn = 1;
                break;
            }
        }
        if (HasBtn) {
            UINT32 BarTop;
            UINT32 Ch;

            Ch = W->Height - TITLE_HEIGHT - 1 - GUI_CLIENT_PAD * 2;
            if (Ch >= 48) {
                /* 与 UserButtonHit：By = Cy+Ch-Bh-4，带延伸到窗底 */
                BarTop = W->Y + TITLE_HEIGHT + GUI_CLIENT_PAD + Ch - 36u - 4u;
                if (Y >= BarTop) {
                    return RESIZE_EDGE_NONE;
                }
            }
        }
    }
    Rx = W->X + W->Width - RESIZE_HOT;
    Ry = W->Y + W->Height - RESIZE_HOT;
    OnRight = (X >= Rx) ? 1 : 0;
    OnBottom = (Y >= Ry) ? 1 : 0;
    if (OnRight && OnBottom) {
        return RESIZE_EDGE_SE;
    }
    if (OnRight) {
        return RESIZE_EDGE_E;
    }
    if (OnBottom) {
        return RESIZE_EDGE_S;
    }
    return RESIZE_EDGE_NONE;
}

int PointInResizeCorner(const GUI_WINDOW *W, UINT32 X, UINT32 Y) {
    return GuiResizeEdgeAt(W, X, Y) != RESIZE_EDGE_NONE ? 1 : 0;
}

int GuiResizeCursorKindAt(UINT32 X, UINT32 Y) {
    int Edge;
    int Top;

    if (gGuiWindowManager.ResizeWin >= 0) {
        Edge = gGuiWindowManager.ResizeEdge;
    } else {
        if (DesktopStartMenuIsOpen() || DesktopContextMenuIsOpen()) {
            return CURSOR_KIND_ARROW;
        }
        Top = TopWindowAt(X, Y);
        if (Top < 0) {
            return CURSOR_KIND_ARROW;
        }
        Edge = GuiResizeEdgeAt(&gGuiWindowManager.Windows[Top], X, Y);
    }
    if (Edge == RESIZE_EDGE_SE) {
        return CURSOR_KIND_RESIZE_SE;
    }
    if (Edge == RESIZE_EDGE_E) {
        return CURSOR_KIND_RESIZE_E;
    }
    if (Edge == RESIZE_EDGE_S) {
        return CURSOR_KIND_RESIZE_S;
    }
    return CURSOR_KIND_ARROW;
}
