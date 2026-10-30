/*
 * IconDrag.c — 桌面图标拖放
 * 核心：Desktop.c
 */
#include "DesktopPrivate.h"
#include "HalVideo.h"

int DesktopIconDragActive(void) {
    return gIconDragIdx >= 0;
}

void DesktopIconDragUpdate(UINT32 X, UINT32 Y) {
    UINT32 Dx;
    UINT32 Dy;
    INT32 Nx;
    INT32 Ny;
    UINT32 Ux;
    UINT32 Uy;

    if (gIconDragIdx < 0 || gIconDragIdx >= DESKTOP_ICON_COUNT) {
        return;
    }
    Dx = (X >= gIconDragStartX) ? (X - gIconDragStartX) : (gIconDragStartX - X);
    Dy = (Y >= gIconDragStartY) ? (Y - gIconDragStartY) : (gIconDragStartY - Y);
    if (!gIconDragMoved) {
        if (Dx <= DESKTOP_DRAG_THRESH && Dy <= DESKTOP_DRAG_THRESH) {
            return;
        }
        gIconDragMoved = 1;
        /* 已进入拖放：清双击时钟，避免松手后再点误开 */
        gSelectClock = 0;
    }
    Nx = (INT32)X - gIconDragOffX;
    Ny = (INT32)Y - gIconDragOffY;
    if (Nx < 0) {
        Nx = 0;
    }
    if (Ny < 0) {
        Ny = 0;
    }
    Ux = (UINT32)Nx;
    Uy = (UINT32)Ny;
    MoveIconTo(gIconDragIdx, Ux, Uy);
}

/* 松手后先擦旧落点、再擦吸附格；Clear 时暂藏本图标，免按新坐标画回旧脚印 */
static void ClearDragFootprints(int Index, UINT32 OldX, UINT32 OldY,
                                UINT32 OldW, UINT32 OldH) {
    UINT32 ScreenW = 0;
    UINT32 ScreenH = 0;
    UINT32 NewX;
    UINT32 NewY;
    UINT32 NewW;
    UINT32 NewH;
    int Present;

    HalVideoGetSize(&ScreenW, &ScreenH);
    if (OldX >= DESKTOP_EDGE_MARGIN) {
        OldX -= DESKTOP_EDGE_MARGIN;
        OldW += DESKTOP_EDGE_MARGIN;
    }
    if (OldY >= DESKTOP_EDGE_MARGIN) {
        OldY -= DESKTOP_EDGE_MARGIN;
        OldH += DESKTOP_EDGE_MARGIN;
    }
    OldW += DESKTOP_EDGE_MARGIN;
    OldH += DESKTOP_EDGE_MARGIN;
    if (ScreenW && OldX + OldW > ScreenW) {
        OldW = ScreenW - OldX;
    }
    if (ScreenH && OldY + OldH > ScreenH) {
        OldH = ScreenH - OldY;
    }
    IconBounds(&gIcons[Index], &NewX, &NewY, &NewW, &NewH);
    if (NewX >= DESKTOP_EDGE_MARGIN) {
        NewX -= DESKTOP_EDGE_MARGIN;
        NewW += DESKTOP_EDGE_MARGIN;
    }
    if (NewY >= DESKTOP_EDGE_MARGIN) {
        NewY -= DESKTOP_EDGE_MARGIN;
        NewH += DESKTOP_EDGE_MARGIN;
    }
    NewW += DESKTOP_EDGE_MARGIN;
    NewH += DESKTOP_EDGE_MARGIN;
    if (ScreenW && NewX + NewW > ScreenW) {
        NewW = ScreenW - NewX;
    }
    if (ScreenH && NewY + NewH > ScreenH) {
        NewH = ScreenH - NewY;
    }
    Present = gIcons[Index].Present;
    gIcons[Index].Present = 0;
    ClearIconFootprint(OldX, OldY, OldW, OldH);
    ClearIconFootprint(NewX, NewY, NewW, NewH);
    gIcons[Index].Present = Present;
}

void DesktopIconDragEnd(void) {
    int Idx;
    int Moved;
    UINT32 OldX;
    UINT32 OldY;
    UINT32 OldW;
    UINT32 OldH;

    Idx = gIconDragIdx;
    Moved = gIconDragMoved;
    /* 先清拖态，避免 SaveIconLayout 阻塞时仍算「拖动中」 */
    gIconDragIdx = -1;
    gIconDragMoved = 0;
    gIconDragOffX = 0;
    gIconDragOffY = 0;

    if (Idx < 0 || Idx >= DESKTOP_ICON_COUNT) {
        return;
    }
    if (!Moved) {
        return;
    }
    /*
     * 必须先 Snap 再擦落点：ClearIconFootprint → DesktopDrawRect 会按
     * gIcons 重画；若坐标仍在松手处，刚擦掉的残影会被立刻画回。
     * 视觉归位优先于 SaveIconLayout（DB/MSC 慢时勿卡住不吸附）。
     */
    IconBounds(&gIcons[Idx], &OldX, &OldY, &OldW, &OldH);
    SnapIconToGrid(&gIcons[Idx].X, &gIcons[Idx].Y);
    ClearDragFootprints(Idx, OldX, OldY, OldW, OldH);
    DrawOneIconOccluded(&gIcons[Idx], Idx == gDeskSelected);
    HalVideoPresentFlush();
    SaveIconLayout();
}
