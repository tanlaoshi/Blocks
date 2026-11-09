/*
 * IgpuHwCursor.c — hw-cursor-igpu / hw-cursor-ddb
 *
 * 契约：Supported/Enable 失败 → Gui 软件末波。热路径只写位置并 CURBASE 武装。
 *
 * Gen9+（对照 i915 skl_cursor_allocation / skl_write_cursor_wm / i9xx_cursor_update_arm）：
 *   开 CUR 前必须有 CUR_BUF_CFG（DDB）与 CUR_WM_EN；无则 FIFO underrun → 黑屏。
 *   固件常 cfg=0：从 primary PLANE_BUF_CFG 尾割 32 块（单 pipe floor），
 *   仅当剩余 > primary WM0 blocks；CUR_WM 复用固件值 | EN（不臆造水位公式）。
 *   缩 primary 后写回 PLANE_SURF 武装。CUR 武装序：BUF/WM → CTL → POS → BASE。
 *
 * DDB / 位图见 IgpuHwCursorDdb.c / IgpuHwCursorBuffer.c。
 */
#include "IgpuHwCursorPrivate.h"
#include "Igpu.h"
#include "IgpuBlitPrivate.h"
#include "HalVideo.h"
#include "SerialLog.h"

int gCapable;
int gProbed;
int gOn;
int gDdbReady;
UINT32 *gArgb;
UINT64 gPhys;
UINT32 gHotX;
UINT32 gHotY;
UINT32 gCurCtl;
UINT32 gCurBase;
UINT32 gCurPos;
UINT32 gCurFbc;
UINT32 gCurrentBufferConfig;
UINT32 gCurWm0;
UINT32 gPlaneBufReg;
UINT32 gPlaneWm0Reg;
UINT32 gSavedBufferConfig;
UINT32 gSavedWm0;
UINT32 gOrigPlaneBuf;
int gCarved;

static int ProbeCapable(void) {
    if (gProbed) {
        return gCapable;
    }
    gProbed = 1;
    if (!IgpuMmioOk() || !IgpuGsmOk() || !IgpuGttOk()) {
        gCapable = 0;
        return 0;
    }
    if (!EnsureCursorDdb()) {
        gCapable = 0;
        return 0;
    }
    gCapable = 1;
    return 1;
}

static void FailLatch(void) {
    if (gOn && IgpuForcewakeGet()) {
        IgpuMmioWrite32(gCurCtl, 0);
        IgpuMmioWrite32(gCurBase, (UINT32)IGPU_CUR_GTT_OFF);
    }
    RestorePlaneDdb();
    gCapable = 0;
    gOn = 0;
}

static UINT32 EncodePos(INT32 X, INT32 Y) {
    UINT32 Pos = 0;
    UINT32 Ax;
    UINT32 Ay;

    if (X < 0) {
        Pos |= CURSOR_POS_X_SIGN;
        Ax = (UINT32)(-X);
    } else {
        Ax = (UINT32)X;
    }
    if (Y < 0) {
        Pos |= CURSOR_POS_Y_SIGN;
        Ay = (UINT32)(-Y);
    } else {
        Ay = (UINT32)Y;
    }
    Pos |= (Ax & 0x7FFFu);
    Pos |= (Ay & 0x7FFFu) << 16;
    return Pos;
}

static void LogToPhys(UINT32 Lx, UINT32 Ly, UINT32 *Px, UINT32 *Py) {
    UINT32 Lw;
    UINT32 Lh;
    UINT32 Pw;
    UINT32 Ph;

    HalVideoGetSize(&Lw, &Lh);
    HalVideoGetPhysicalSize(&Pw, &Ph);
    if (Lw == 0 || Lh == 0 || Pw == 0 || Ph == 0 || (Lw == Pw && Lh == Ph)) {
        *Px = Lx;
        *Py = Ly;
        return;
    }
    *Px = (UINT32)(((UINT64)Lx * (UINT64)Pw) / (UINT64)Lw);
    *Py = (UINT32)(((UINT64)Ly * (UINT64)Ph) / (UINT64)Lh);
}

static void ArmCursor(UINT32 Ctl, UINT32 Pos) {
    IgpuMmioWrite32(gCurrentBufferConfig, gSavedBufferConfig);
    IgpuMmioWrite32(gCurWm0, gSavedWm0);
    /*
     * IVB+ CUR_FBC_CTL 的 HEIGHT 字段会在方 cursors 之外限制扫描行数：
     * 固件常留 EN|N（N 可能是 16/24），64×64 位图只有顶部 N 行上屏，
     * 表现为 150%/200% 放大后「尾巴被裁」「只剩左上角」。i915 对
     * width==height 写 0；这里同样在每次 ARM 前清零，绝不能沿用固件值。
     */
    IgpuMmioWrite32(gCurFbc, 0);
    IgpuMmioWrite32(gCurCtl, Ctl);
    IgpuMmioWrite32(gCurPos, Pos);
    IgpuMmioWrite32(gCurBase, (UINT32)IGPU_CUR_GTT_OFF);
}

static void WritePos(UINT32 LogX, UINT32 LogY) {
    UINT32 Px;
    UINT32 Py;
    INT32 Cx;
    INT32 Cy;
    UINT32 Pos;

    LogToPhys(LogX, LogY, &Px, &Py);
    Cx = (INT32)Px - (INT32)gHotX;
    Cy = (INT32)Py - (INT32)gHotY;
    Pos = EncodePos(Cx, Cy);
    if (!IgpuForcewakeGet()) {
        return;
    }
    IgpuMmioWrite32(gCurPos, Pos);
    IgpuMmioWrite32(gCurBase, (UINT32)IGPU_CUR_GTT_OFF);
}

int HalVideoHwCursorSupported(void) {
    return ProbeCapable();
}

int HalVideoHwCursorEnable(const UINT32 *Argb, UINT32 Width, UINT32 Height,
                           UINT32 HotX, UINT32 HotY) {
    if (!ProbeCapable()) {
        return 0;
    }
    if (!EnsureCursorDdb()) {
        FailLatch();
        return 0;
    }
    if (gOn) {
        HalVideoHwCursorDisable();
    }
    if (!EnsureBuffer()) {
        FailLatch();
        LogDrv("Boot: igpu hw cursor fail (buf)\n");
        return 0;
    }

    CopyGlyph(Argb, Width, Height, gArgb);
    FlushCpu(gArgb, IGPU_CUR_BYTES);

    if (Argb && Width != 0 && Height != 0) {
        gHotX = HotX;
        gHotY = HotY;
        if (gHotX >= IGPU_CUR_SIZE) {
            gHotX = 0;
        }
        if (gHotY >= IGPU_CUR_SIZE) {
            gHotY = 0;
        }
    } else {
        gHotX = 1;
        gHotY = 1;
    }

    if (!IgpuForcewakeGet()) {
        FailLatch();
        LogDrv("Boot: igpu hw cursor fail (fw)\n");
        return 0;
    }

    ArmCursor(MCURSOR_MODE_64_ARGB_AX, EncodePos(0, 0));

    gOn = 1;
    LogDrv("Boot: igpu hw cursor ok w=");
    LogDrvHex32(Width);
    LogDrv(" h=");
    LogDrvHex32(Height);
    LogDrv(" hot=");
    LogDrvHex32(gHotX);
    LogDrv(",");
    LogDrvHex32(gHotY);
    LogDrv(" fbc=");
    LogDrvHex32(IgpuMmioRead32(gCurFbc));
    LogDrv(" ctl=");
    LogDrvHex32(IgpuMmioRead32(gCurCtl));
    LogDrv(" pte3=");
    LogDrvHex32((UINT32)IgpuGsmPteRead(IGPU_CUR_GTT_OFF + 3ull * 4096ull));
    LogDrv(" cfg=");
    LogDrvHex32(gSavedBufferConfig);
    LogDrv("\n");
    return 1;
}

void HalVideoHwCursorSetPos(UINT32 X, UINT32 Y) {
    if (!gOn) {
        return;
    }
    WritePos(X, Y);
}

void HalVideoHwCursorDisable(void) {
    if (!gOn) {
        return;
    }
    if (IgpuForcewakeGet()) {
        ArmCursor(0, 0);
    }
    gOn = 0;
}
