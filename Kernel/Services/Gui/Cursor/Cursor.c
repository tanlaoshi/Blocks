/*
 * Cursor.c — 鼠标光标 save-under + 移动/显隐（gui-g7-cursor）
 *
 * 字形 Shape.c；几何 CursorGeometry.c；硬件 CursorHardware.c；
 * Reveal/显隐 CursorReveal.c。改大小悬停换形 GuiResizeCursorKindAt。
 *
 * ── 光标契约（对表拧，勿凭感觉改序）──
 *
 * 原语：
 *   CursorRestore — Overlay 内 WriteRect(Under) 盖回；清 Visible
 *   CursorPaint   — 若 Visible 先 Restore → ReadRect→Under → 画字形 → Visible=1
 *   二者写后缓冲时必 CursorOverlayBegin/End → 脏进 gCurDirty（≠ 内容 gDirty）
 *
 * PR-23c：save-under 走物理像素（HalVideoReadRectPhys/WriteRectPhys）。
 *   Under 按 CURSOR_BOX_PHYS² 预留；Save* 仍存逻辑值。
 *
 * 合成序（Compose / ThemeScene / FrameBuffer / Redraw）：
 *   1 ComposeBeginEraseCursor：先 Restore 再 ComposeBusy（见 Compose.c）
 *   2 开中断画桌面/窗（Busy 丢弃嵌套鼠标）
 *   3 Irq 内 CursorPaint
 *   4 PresentFlush 仍在 ComposeBusy 内（G6）
 *
 * 移动 CursorMove：
 *   常态：Irq 内 Restore+Paint → 开中断一次 PresentCursor
 *   拖窗/拖图标：只 Restore+PresentCursor，不 Paint
 *
 * 光标门闩：GuiCursorUsbReady / GuiCursorRevealAfterUsb（CursorReveal.c）。
 * 硬件光标：CursorHardware.c。
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

void CursorRestore(void) {
    UINT32 Xp;
    UINT32 Yp;
    UINT32 Wp;
    UINT32 Hp;

    if (CursorHwActive()) {
        /* FB 无光标像素；仍清 Visible，免后续软路径误 Restore Under */
        gGuiWindowManager.CursorVisible = 0;
        return;
    }
    if (!gGuiWindowManager.CursorVisible) {
        return;
    }
    /*
     * 限幅按逻辑盒子（CURSOR_BOX）判定；Under 已按最大倍数预留，
     * 换算成物理后必然装得下——勿拿物理尺寸去比 CURSOR_BOX，否则 200%
     * 下整段跳过写回，就成了光标残影。
     */
    if (gGuiWindowManager.SaveWidth > 0 && gGuiWindowManager.SaveHeight > 0 &&
        gGuiWindowManager.SaveWidth <= (UINT32)CURSOR_BOX &&
        gGuiWindowManager.SaveHeight <= (UINT32)CURSOR_BOX) {
        CursorPhysicalRect(gGuiWindowManager.SaveX, gGuiWindowManager.SaveY,
                       gGuiWindowManager.SaveWidth, gGuiWindowManager.SaveHeight,
                       &Xp, &Yp, &Wp, &Hp);
        HalVideoCursorOverlayBegin();
        HalVideoWriteRectPhys(Xp, Yp, Wp, Hp, gGuiWindowManager.Under);
        HalVideoCursorOverlayEnd();
    }
    gGuiWindowManager.CursorVisible = 0;
}

void CursorPaint(void) {
    UINT32 Xp;
    UINT32 Yp;
    UINT32 Wp;
    UINT32 Hp;

    if (!GuiCursorUsbReady()) {
        return;
    }
    if (CursorHwActive()) {
        /* 缩放档或 Kind 变了都要补上载（同坐标 CursorMove 会早退） */
        if (CursorHwRatioChanged() ||
            gGuiWindowManager.CursorKind != CursorHwKind()) {
            (void)CursorHwUploadKind(gGuiWindowManager.CursorKind);
        }
        HalVideoHwCursorSetPos(gGuiWindowManager.CursorX, gGuiWindowManager.CursorY);
        return;
    }
    gGuiWindowManager.CursorKind =
        GuiResizeCursorKindAt(gGuiWindowManager.CursorX, gGuiWindowManager.CursorY);
    if (gGuiWindowManager.CursorVisible) {
        CursorRestore();
    }
    CursorBox(gGuiWindowManager.CursorX, gGuiWindowManager.CursorY,
              &gGuiWindowManager.SaveX, &gGuiWindowManager.SaveY,
              &gGuiWindowManager.SaveWidth, &gGuiWindowManager.SaveHeight);
    if (gGuiWindowManager.SaveWidth == 0 || gGuiWindowManager.SaveHeight == 0 ||
        gGuiWindowManager.SaveWidth > (UINT32)CURSOR_BOX ||
        gGuiWindowManager.SaveHeight > (UINT32)CURSOR_BOX) {
        return;
    }
    CursorPhysicalRect(gGuiWindowManager.SaveX, gGuiWindowManager.SaveY,
                   gGuiWindowManager.SaveWidth, gGuiWindowManager.SaveHeight,
                   &Xp, &Yp, &Wp, &Hp);
    HalVideoCursorOverlayBegin();
    /* 物理像素读回：块内细节全留住，Restore 才能原样还原（PR-23c） */
    HalVideoReadRectPhys(Xp, Yp, Wp, Hp, gGuiWindowManager.Under);
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
    if (!GuiCursorUsbReady()) {
        return;
    }
    OldKind = gGuiWindowManager.CursorKind;
    NewKind = GuiResizeCursorKindAt(X, Y);
    if (X == gGuiWindowManager.CursorX && Y == gGuiWindowManager.CursorY &&
        OldKind == NewKind) {
        return;
    }

    if (CursorHwActive()) {
        gGuiWindowManager.CursorX = X;
        gGuiWindowManager.CursorY = Y;
        gGuiWindowManager.CursorKind = NewKind;
        if (CursorHwRatioChanged() || NewKind != CursorHwKind()) {
            /* 换形/换缩放失败保留旧字形，勿半截回软 */
            (void)CursorHwUploadKind(NewKind);
        }
        HalVideoHwCursorSetPos(X, Y);
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

    /*
     * 软件末波：Restore 旧盒 + Paint 新盒，一次 PresentCursor。
     */
    GraphicsIrqEnter();
    CursorRestore();
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
