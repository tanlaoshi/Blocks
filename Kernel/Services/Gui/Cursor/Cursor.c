/*
 * GuiCursor.c — 鼠标光标：save-under + 移动/显隐
 *
 * 字形见 GuiCursorShape.c。改大小悬停换形见 GuiResizeCursorKindAt。
 */
#include "GuiPrivate.h"
#include "HalVideo.h"
#include "Hal.h"
#include "UI.h"
#include "FilesUi.h"
#include "SettingsUi.h"
#include "StoreUi.h"
#include "EditUi.h"
#include "Desktop.h"

void CursorBox(UINT32 Cx, UINT32 Cy, UINT32 *Sx, UINT32 *Sy,
                      UINT32 *Sw, UINT32 *Sh) {
    int Half;
    int Thick;
    int L;
    int R;
    int T;
    int B;
    UINT32 ExtPad;
    INT32 X0;
    INT32 Y0;
    INT32 X1;
    INT32 Y1;

    CursorMetrics(&Half, &Thick);
    ExtPad = 0;
    if (HalVideoGetUiScale() != 100u) {
        ExtPad = 2u;
    }
    if (gGuiWindowManager.CursorKind == CURSOR_KIND_ARROW) {
        /*
         * 尖在热点；头约 1.5H、尾再约 0.5H。多留边防拖尾。
         */
        L = Half / 2 + CURSOR_OUTLINE + (int)ExtPad + 4;
        T = Half / 2 + CURSOR_OUTLINE + (int)ExtPad + 4;
        R = Half * 3 + CURSOR_OUTLINE + (int)ExtPad + 6;
        B = Half * 3 + CURSOR_OUTLINE + (int)ExtPad + 6;
    } else {
        /* resize：热点居中 */
        L = R = T = B = Half + Thick + CURSOR_OUTLINE + (int)ExtPad;
        if (gGuiWindowManager.CursorKind == CURSOR_KIND_RESIZE_SE) {
            L = R = T = B = Half + Thick + CURSOR_OUTLINE + 2 + (int)ExtPad;
        }
    }
    X0 = (INT32)Cx - L;
    Y0 = (INT32)Cy - T;
    X1 = (INT32)Cx + R + 1;
    Y1 = (INT32)Cy + B + 1;
    if (X0 < 0) {
        X0 = 0;
    }
    if (Y0 < 0) {
        Y0 = 0;
    }
    if (X1 > (INT32)gGuiWindowManager.ScreenWidth) {
        X1 = (INT32)gGuiWindowManager.ScreenWidth;
    }
    if (Y1 > (INT32)gGuiWindowManager.ScreenHeight) {
        Y1 = (INT32)gGuiWindowManager.ScreenHeight;
    }
    *Sx = (UINT32)X0;
    *Sy = (UINT32)Y0;
    *Sw = (UINT32)(X1 - X0);
    *Sh = (UINT32)(Y1 - Y0);
}

void DrawCursorAt(UINT32 X, UINT32 Y) {
    DrawCursorGlyph(X, Y);
}

void CursorRestore(void) {
    if (!gGuiWindowManager.CursorVisible) {
        return;
    }
    if (gGuiWindowManager.SaveWidth > 0 && gGuiWindowManager.SaveHeight > 0 &&
        gGuiWindowManager.SaveWidth <= (UINT32)CURSOR_BOX && gGuiWindowManager.SaveHeight <= (UINT32)CURSOR_BOX) {
        HalVideoCursorOverlayBegin();
        HalVideoWriteRect(gGuiWindowManager.SaveX, gGuiWindowManager.SaveY, gGuiWindowManager.SaveWidth, gGuiWindowManager.SaveHeight, gGuiWindowManager.Under);
        HalVideoCursorOverlayEnd();
    }
    gGuiWindowManager.CursorVisible = 0;
}

void CursorPaint(void) {
    gGuiWindowManager.CursorKind = GuiResizeCursorKindAt(gGuiWindowManager.CursorX, gGuiWindowManager.CursorY);
    if (gGuiWindowManager.CursorVisible) {
        CursorRestore();
    }
    CursorBox(gGuiWindowManager.CursorX, gGuiWindowManager.CursorY, &gGuiWindowManager.SaveX, &gGuiWindowManager.SaveY, &gGuiWindowManager.SaveWidth, &gGuiWindowManager.SaveHeight);
    if (gGuiWindowManager.SaveWidth == 0 || gGuiWindowManager.SaveHeight == 0 ||
        gGuiWindowManager.SaveWidth > (UINT32)CURSOR_BOX || gGuiWindowManager.SaveHeight > (UINT32)CURSOR_BOX) {
        return;
    }
    HalVideoCursorOverlayBegin();
    HalVideoReadRect(gGuiWindowManager.SaveX, gGuiWindowManager.SaveY, gGuiWindowManager.SaveWidth, gGuiWindowManager.SaveHeight, gGuiWindowManager.Under);
    DrawCursorGlyph(gGuiWindowManager.CursorX, gGuiWindowManager.CursorY);
    HalVideoCursorOverlayEnd();
    gGuiWindowManager.CursorVisible = 1;
}

void CursorMove(UINT32 X, UINT32 Y) {
    int OldKind;
    int NewKind;

    if (X >= gGuiWindowManager.ScreenWidth) {
        X = gGuiWindowManager.ScreenWidth > 0 ? gGuiWindowManager.ScreenWidth - 1 : 0;
    }
    if (Y >= gGuiWindowManager.ScreenHeight) {
        Y = gGuiWindowManager.ScreenHeight > 0 ? gGuiWindowManager.ScreenHeight - 1 : 0;
    }
    OldKind = gGuiWindowManager.CursorKind;
    NewKind = GuiResizeCursorKindAt(X, Y);
    if (X == gGuiWindowManager.CursorX && Y == gGuiWindowManager.CursorY && OldKind == NewKind) {
        return;
    }

    if (gGuiWindowManager.DragWin >= 0 || DesktopIconDragActive()) {
        if (gGuiWindowManager.CursorVisible) {
            GraphicsIrqEnter();
            CursorRestore();
            GraphicsPresent();
            GraphicsIrqLeave();
        }
        gGuiWindowManager.CursorX = X;
        gGuiWindowManager.CursorY = Y;
        gGuiWindowManager.CursorKind = NewKind;
        return;
    }

    GraphicsIrqEnter();
    CursorRestore();
    gGuiWindowManager.CursorX = X;
    gGuiWindowManager.CursorY = Y;
    gGuiWindowManager.CursorKind = NewKind;
    CursorPaint();
    GraphicsPresent();
    GraphicsIrqLeave();
}

void GuiPointerMove(UINT32 X, UINT32 Y) {
    if (!(gGuiWindowManager.DragWin >= 0 && (gGuiWindowManager.CursorBtn & 1)) &&
        !(gGuiWindowManager.ResizeWin >= 0 && (gGuiWindowManager.CursorBtn & 1)) &&
        !(DesktopIconDragActive() && (gGuiWindowManager.CursorBtn & 1))) {
        GuiHoverUpdate(X, Y);
    }
    CursorMove(X, Y);
    if (gGuiWindowManager.ResizeWin >= 0 && (gGuiWindowManager.CursorBtn & 1)) {
        GuiResizeUpdate(X, Y);
    } else if (gGuiWindowManager.DragWin >= 0 && (gGuiWindowManager.CursorBtn & 1)) {
        GuiDragUpdate(X, Y);
    } else if (DesktopIconDragActive() && (gGuiWindowManager.CursorBtn & 1)) {
        DesktopIconDragUpdate(X, Y);
    } else if (GuiFocusKind() == GUI_WIN_FILES) {
        FilesUiOnHover(X, Y);
    } else if (GuiFocusKind() == GUI_WIN_SETTINGS) {
        SettingsUiOnPointer(X, Y, gGuiWindowManager.CursorBtn);
    } else if (GuiFocusKind() == GUI_WIN_STORE) {
        if (!StoreUiIsBusy()) {
            StoreUiOnPointer(X, Y, gGuiWindowManager.CursorBtn);
        }
    } else if (GuiFocusKind() == GUI_WIN_EDIT) {
        EditUiOnPointer(X, Y, gGuiWindowManager.CursorBtn);
    }
}

void GuiFrameBufferBegin(void) {
    ComposeBeginEraseCursor();
}

void GuiFrameBufferEnd(void) {
    GraphicsIrqEnter();
    CursorPaint();
    GraphicsIrqLeave();
    /*
     * Shell 命令 DeferPresent 期间勿逐字/逐行 Flush：
     * 否则 help 等会一行一帧 Present，真机极卡。
     * DeferPop 时统一 Present。
     */
    if (!gGuiWindowManager.DeferPresent) {
        HalVideoPresentFlush();
    }
    ComposeEnd();
}

void GuiCursorPaint(void) {
    GraphicsIrqEnter();
    CursorRestore();
    CursorPaint();
    GraphicsPresent();
    GraphicsIrqLeave();
}

void GuiCursorHide(void) {
    GraphicsIrqEnter();
    CursorRestore();
    GraphicsPresent();
    GraphicsIrqLeave();
}

void GuiCursorShow(void) {
    GraphicsIrqEnter();
    CursorPaint();
    GraphicsPresent();
    GraphicsIrqLeave();
}
