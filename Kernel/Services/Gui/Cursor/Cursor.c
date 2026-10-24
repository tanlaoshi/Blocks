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
 *   常态：Irq 内 Restore+Paint → 开中断一次 PresentCursor（Poll 已并末坐标）
 *   勿 Restore/Paint 各 Present：igpu 双缓冲曾各同步 blit → <5s 拖影后只闪
 *   拖窗/拖图标：只 Restore+PresentCursor，不 Paint
 *
 * 对外：GuiCursorHide/Show/Paint、GuiFrameBufferBegin/End — 同文件下部。
 * HAL 分脏 / XOR：Pixel.c；Flush vs Present：Present.c（G6）。
 *
 * 光标门闩（CursorUsbReady）：
 *   HID 开机早绑；桌面先出、不 Present / 不跟坐标。
 *   Worker 网/iwl 静默后、AnnounceBootReady 前 Reveal：居中 + Handoff + 画出。
 *   契约：一露就必须流畅；慢活未完勿 Reveal。
 *
 * 硬件光标（hw-cursor-*）：
 *   Enable 成功 → gHwCursorOn：Move 只 HalVideoHwCursorSetPos；Compose 勿 Restore FB。
 *   Supported=0 / Enable 失败 → 软件末波（本文件 Restore+Paint+PresentCursor）。
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
/* 1：HalVideoHwCursorEnable 已成功；热路径只写位置 */
static int gHwCursorOn;
/* 已上载的 Kind；换形重 Enable */
static int gHwKind = -1;
static UINT32 gHwGlyph[CURSOR_HW_DIM * CURSOR_HW_DIM];

int CursorHwActive(void) {
    return gHwCursorOn ? 1 : 0;
}

static void CursorHwEraseSoftFb(void) {
    /* 开 Hw 前擦净 FB 软像素；否则 CUR 叠在软影上 → 双光标 */
    if (!gGuiWindowManager.CursorVisible) {
        return;
    }
    GraphicsIrqEnter();
    CursorRestore();
    GraphicsIrqLeave();
    HalVideoPresentCursor();
}

static int CursorHwUploadKind(int Kind) {
    UINT32 HotX;
    UINT32 HotY;

    if (!CursorGlyphToArgb(Kind, gHwGlyph, &HotX, &HotY)) {
        return 0;
    }
    if (!HalVideoHwCursorEnable(gHwGlyph, CURSOR_HW_DIM, CURSOR_HW_DIM, HotX, HotY)) {
        return 0;
    }
    gHwKind = Kind;
    return 1;
}

static void CursorHwTryEnable(void) {
    if (gHwCursorOn) {
        return;
    }
    if (!HalVideoHwCursorSupported()) {
        return;
    }
    CursorHwEraseSoftFb();
    if (!CursorHwUploadKind(gGuiWindowManager.CursorKind)) {
        return;
    }
    gHwCursorOn = 1;
    gGuiWindowManager.CursorVisible = 0;
    HalVideoHwCursorSetPos(gGuiWindowManager.CursorX, gGuiWindowManager.CursorY);
    DebugWrite("Gui: hw cursor on\n");
}

static void CursorHwDisable(void) {
    if (!gHwCursorOn) {
        return;
    }
    HalVideoHwCursorDisable();
    gHwCursorOn = 0;
    gHwKind = -1;
}

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
    if (gHwCursorOn) {
        /* FB 无光标像素；仍清 Visible，免后续软路径误 Restore Under */
        gGuiWindowManager.CursorVisible = 0;
        return;
    }
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
    if (gHwCursorOn) {
        /* ResizeBegin 已改 Kind 时补上载（同坐标 CursorMove 会早退） */
        if (gGuiWindowManager.CursorKind != gHwKind) {
            (void)CursorHwUploadKind(gGuiWindowManager.CursorKind);
        }
        HalVideoHwCursorSetPos(gGuiWindowManager.CursorX, gGuiWindowManager.CursorY);
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

    if (gHwCursorOn) {
        gGuiWindowManager.CursorX = X;
        gGuiWindowManager.CursorY = Y;
        gGuiWindowManager.CursorKind = NewKind;
        if (NewKind != gHwKind) {
            /* 换形失败保留旧字形，勿半截回软 */
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

void GuiCursorPaint(void) {
    GraphicsIrqEnter();
    CursorRestore();
    CursorPaint();
    GraphicsIrqLeave();
    GraphicsPresent();
}

void GuiCursorHide(void) {
    if (gHwCursorOn) {
        CursorHwDisable();
        return;
    }
    GraphicsIrqEnter();
    CursorRestore();
    GraphicsIrqLeave();
    GraphicsPresent();
}

void GuiCursorShow(void) {
    if (!gCursorUsbReady) {
        return;
    }
    CursorHwTryEnable();
    if (gHwCursorOn) {
        return;
    }
    GraphicsIrqEnter();
    CursorPaint();
    GraphicsIrqLeave();
    GraphicsPresent();
}

/* 最近一次指针位移（HalCpuTicks）；供钟栏/HotPoll 让路 */
static UINT64 gPointerActivityTick;

void GuiPointerNoteActivity(void) {
    gPointerActivityTick = HalCpuTicks(0);
}

int GuiPointerIdleForMs(UINT32 Ms) {
    UINT64 Now;
    UINT32 Tps;
    UINT64 Need;

    if (gPointerActivityTick == 0) {
        return 1;
    }
    Now = HalCpuTicks(0);
    Tps = HalTicksPerSec();
    if (Tps == 0) {
        Tps = 250;
    }
    Need = ((UINT64)Tps * (UINT64)Ms) / 1000ull;
    if (Need == 0) {
        Need = 1;
    }
    return (Now - gPointerActivityTick) >= Need ? 1 : 0;
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
    /*
     * 先试 Hw 再置 ready：避免 ready=1 后、Show 前 Compose 软画，
     * 随后 Enable 不擦 → 双光标（小=CUR，大=FB 残影）。
     */
    CursorHwTryEnable();
    gCursorUsbReady = 1;
    DebugWrite("Gui: cursor after usb secondary (center)\n");
    if (!gHwCursorOn) {
        GraphicsIrqEnter();
        CursorPaint();
        GraphicsIrqLeave();
        /* PresentCursor（含 Pi FB dcache sync）；勿只用 GraphicsPresent */
        HalVideoPresentCursor();
    }
}
