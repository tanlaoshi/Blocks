/*
 * CursorHardware.c — 硬件光标 Enable / 上载 / 位置（自 Cursor.c 纯搬）
 *
 * Enable 成功 → CursorHwActive：Move 只 HalVideoHwCursorSetPos；Compose 勿 Restore FB。
 * Supported=0 / Enable 失败 → 软件末波（Cursor.c Restore+Paint+PresentCursor）。
 */
#include "GuiPrivate.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Debug.h"
#include "SerialLog.h"

/* 1：HalVideoHwCursorEnable 已成功；热路径只写位置 */
static int gHwCursorOn;
/* 已上载的 Kind；换形重 Enable */
static int gHwKind = -1;
/* 已上载位图所用的绘制比例；缩放档切换后必须重栅，不能只换位置 */
static UINT32 gHwNum = 1;
static UINT32 gHwDen = 1;
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

static void CursorHwLogGlyphBBox(const UINT32 *Pix) {
    UINT32 X;
    UINT32 Y;
    int MinX = CURSOR_HW_DIM;
    int MaxX = -1;
    int MinY = CURSOR_HW_DIM;
    int MaxY = -1;

    for (Y = 0; Y < CURSOR_HW_DIM; Y++) {
        for (X = 0; X < CURSOR_HW_DIM; X++) {
            if ((Pix[Y * CURSOR_HW_DIM + X] & 0xFF000000u) == 0) {
                continue;
            }
            if ((int)X < MinX) {
                MinX = (int)X;
            }
            if ((int)X > MaxX) {
                MaxX = (int)X;
            }
            if ((int)Y < MinY) {
                MinY = (int)Y;
            }
            if ((int)Y > MaxY) {
                MaxY = (int)Y;
            }
        }
    }
    if (MaxX < 0) {
        LogDrv(" bbox=empty");
        return;
    }
    LogDrv(" bbox=");
    LogDrvHex32((UINT32)MinX);
    LogDrv("-");
    LogDrvHex32((UINT32)MaxX);
    LogDrv(",");
    LogDrvHex32((UINT32)MinY);
    LogDrv("-");
    LogDrvHex32((UINT32)MaxY);
}

int CursorHwUploadKind(int Kind) {
    UINT32 HotX;
    UINT32 HotY;

    if (!CursorGlyphToArgb(Kind, gHwGlyph, &HotX, &HotY)) {
        return 0;
    }
    if (!HalVideoHwCursorEnable(gHwGlyph, CURSOR_HW_DIM, CURSOR_HW_DIM, HotX, HotY)) {
        return 0;
    }
    gHwKind = Kind;
    gHwNum = HalVideoGetDrawNum();
    gHwDen = HalVideoGetDrawDen();
    LogDrv("Cursor: hw upload kind=");
    LogDrvHex32((UINT32)Kind);
    LogDrv(" dim=");
    LogDrvHex32(CURSOR_HW_DIM);
    LogDrv(" hot=");
    LogDrvHex32(HotX);
    LogDrv(",");
    LogDrvHex32(HotY);
    LogDrv(" num=");
    LogDrvHex32(gHwNum);
    LogDrv(" den=");
    LogDrvHex32(gHwDen);
    CursorHwLogGlyphBBox(gHwGlyph);
    LogDrv("\n");
    return 1;
}

/* 缩放档变了：位图边长/内容都变了，必须按新比例重上载 */
int CursorHwRatioChanged(void) {
    UINT32 Num = HalVideoGetDrawNum();
    UINT32 Den = HalVideoGetDrawDen();

    if (Num == 0) {
        Num = 1;
    }
    if (Den == 0) {
        Den = 1;
    }
    return (Num != gHwNum || Den != gHwDen) ? 1 : 0;
}

void CursorHwTryEnable(void) {
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

void CursorHwDisable(void) {
    if (!gHwCursorOn) {
        return;
    }
    HalVideoHwCursorDisable();
    gHwCursorOn = 0;
    gHwKind = -1;
}

int CursorHwKind(void) {
    return gHwKind;
}
