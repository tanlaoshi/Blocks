/*
 * Cursor.c — 鼠标光标 save-under + 移动/显隐（gui-g7-cursor）
 *
 * 字形 Shape.c；改大小悬停换形 GuiResizeCursorKindAt。
 *
 * ── 光标契约（对表拧，勿凭感觉改序）──
 *
 * 原语：
 *   CursorRestore — Overlay 内 WriteRect(Under) 盖回；清 Visible
 *   CursorPaint   — 若 Visible 先 Restore → ReadRect→Under → 画字形 → Visible=1
 *   二者写后缓冲时必 CursorOverlayBegin/End → 脏进 gCurDirty（≠ 内容 gDirty）
 *
 * 合成序（Compose / ThemeScene / FrameBuffer / Redraw）：
 *   1 ComposeBeginEraseCursor：先 Restore 再 ComposeBusy（见 Compose.c）
 *   2 开中断画桌面/窗（Busy 丢弃嵌套鼠标）
 *   3 Irq 内 CursorPaint
 *   4 PresentFlush 仍在 ComposeBusy 内（G6）— 出 Busy 再 Flush → AP 鼠
 *     用旧 Under Restore → 正方形镂空 / 十字印
 *
 * 移动 CursorMove：
 *   常态：Irq 内 Restore → 开中断 PresentCursor → Irq 内改 XY+Paint → PresentCursor
 *   （Present 勿持 IrqSave：双 LivePatch cli 会饿死 xHCI → 空拖后光标只闪不动）
 *   各刷小框；勿靠 DeferPop 并 AABB（空桌面甩鼠大块 blit 顿）
 *   拖窗/拖图标：只 Restore+PresentCursor，不 Paint
 *
 * 对外：GuiCursorHide/Show/Paint、GuiFrameBufferBegin/End — 同文件下部。
 * HAL 分脏 / XOR：Pixel.c；Flush vs Present：Present.c（G6）。
 *
 * usb 慢认门闩（CursorUsbReady）：
 *   HID 开机早绑；桌面先出、不 Present / 不跟坐标（避免 msc/3 Address 抢环）。
 *   Worker EnsureSecondary 后 Reveal：居中 + Handoff + 首次 Paint+Present。
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
#include "Debug.h"

/* 0：Secondary 未完，不画/不 Present；Reveal 置 1 */
static int gCursorUsbReady;

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
    if (!gCursorUsbReady) {
        return;
    }
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
    /* USB 慢认中：不跟坐标、不 Present；Reveal 时再居中 */
    if (!gCursorUsbReady) {
        return;
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
            GraphicsIrqLeave();
            HalVideoPresentCursor();
        }
        gGuiWindowManager.CursorX = X;
        gGuiWindowManager.CursorY = Y;
        gGuiWindowManager.CursorKind = NewKind;
        return;
    }

    GraphicsIrqEnter();
    CursorRestore();
    GraphicsIrqLeave();
    HalVideoPresentCursor();

    GraphicsIrqEnter();
    gGuiWindowManager.CursorX = X;
    gGuiWindowManager.CursorY = Y;
    gGuiWindowManager.CursorKind = NewKind;
    CursorPaint();
    GraphicsIrqLeave();
    HalVideoPresentCursor();
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
    GraphicsIrqLeave();
    GraphicsPresent();
}

void GuiCursorHide(void) {
    GraphicsIrqEnter();
    CursorRestore();
    GraphicsIrqLeave();
    GraphicsPresent();
}

void GuiCursorShow(void) {
    if (!gCursorUsbReady) {
        return;
    }
    GraphicsIrqEnter();
    CursorPaint();
    GraphicsIrqLeave();
    GraphicsPresent();
}

int GuiCursorUsbReady(void) {
    return gCursorUsbReady ? 1 : 0;
}

void GuiCursorRevealAfterUsb(void) {
    UINT32 Cx;
    UINT32 Cy;

    if (gCursorUsbReady) {
        return;
    }
    gCursorUsbReady = 1;
    Cx = gGuiWindowManager.ScreenWidth / 2;
    Cy = gGuiWindowManager.ScreenHeight / 2;
    if (gGuiWindowManager.ScreenWidth == 0) {
        Cx = 512;
    }
    if (gGuiWindowManager.ScreenHeight == 0) {
        Cy = 384;
    }
    gGuiWindowManager.CursorX = Cx;
    gGuiWindowManager.CursorY = Cy;
    gGuiWindowManager.CursorBtn = 0;
    gGuiWindowManager.MousePrevBtn = 0;
    /* 驱动 Abs 与 Gui 居中对齐，首动勿从慢认期积压坐标飞角 */
    HalInputMouseHandoffDesktop(Cx, Cy);
    DebugWrite("Gui: cursor after usb secondary (center)\n");
    GuiCursorShow();
}
