/*
 * IgpuHwCursor.c — hw-cursor-igpu：CUR_CTL/BASE/POS + GTT ARGB
 *
 * 契约：Supported/Enable 失败 → Gui 软件末波。热路径只写位置并 CURBASE 武装。
 *
 * Gen9+（对照 i915 i9xx_cursor_update_arm / skl_write_cursor_wm）：
 *   开 CUR 前必须有 CUR_BUF_CFG（DDB）与 CUR_WM；无则 FIFO underrun → 闪后黑屏。
 *   禁止臆造 DDB/水位；固件未分给光标则 Supported=0，不写 CURCNTR 使能。
 *   武装序：CURCNTR → CURPOS → CURBASE（BASE 最后）。
 */
#include "Igpu.h"
#include "IgpuBlitPrivate.h"
#include "HalVideo.h"
#include "PhysicalMemory.h"
#include "VirtualMemory.h"
#include "SerialLog.h"

#define IGPU_CUR_GTT_OFF       0x05900000ull /* 避开 ring/scratch/batch/back/probe/flip */
#define IGPU_CUR_SIZE          64u
#define IGPU_CUR_BYTES         (IGPU_CUR_SIZE * IGPU_CUR_SIZE * 4u)
#define IGPU_CUR_PAGES         ((IGPU_CUR_BYTES + 4095u) / 4096u)

#define IGPU_REG_CUR_CTL_A     0x70080u
#define IGPU_REG_CUR_BASE_A    0x70084u
#define IGPU_REG_CUR_POS_A     0x70088u
#define IGPU_REG_CUR_BUF_CFG_A 0x7017Cu
#define IGPU_REG_CUR_WM_A0     0x70140u
#define IGPU_REG_PLANE_SURF_B  0x7119Cu
#define IGPU_REG_PLANE_SURF_C  0x7219Cu

#define MCURSOR_MODE_64_ARGB_AX 0x27u
#define CURSOR_POS_X_SIGN      (1u << 15)
#define CURSOR_POS_Y_SIGN      (1u << 31)
#define CUR_WM_EN              (1u << 31)
#define CUR_BUF_START_MASK     0xFFFu
#define CUR_BUF_END_SHIFT      16
#define CUR_BUF_END_MASK       0xFFFu

static int gCapable;   /* 1=可试 Enable；失败后清 0 */
static int gProbed;
static int gOn;
static UINT32 *gArgb;
static UINT64 gPhys;
static UINT32 gHotX;
static UINT32 gHotY;
static UINT32 gCurCtl;
static UINT32 gCurBase;
static UINT32 gCurPos;
static UINT32 gCurBufCfg;
static UINT32 gCurWm0;
static UINT32 gSavedBufCfg;
static UINT32 gSavedWm0;

static void PickCursorRegs(void) {
    UINT32 SurfReg;
    UINT32 PipeOff;

    SurfReg = IgpuGttSurfReg();
    if (SurfReg == IGPU_REG_PLANE_SURF_B) {
        PipeOff = 0x1000u;
    } else if (SurfReg == IGPU_REG_PLANE_SURF_C) {
        PipeOff = 0x2000u;
    } else {
        PipeOff = 0;
    }
    gCurCtl = IGPU_REG_CUR_CTL_A + PipeOff;
    gCurBase = IGPU_REG_CUR_BASE_A + PipeOff;
    gCurPos = IGPU_REG_CUR_POS_A + PipeOff;
    /* CUR_BUF_CFG / CUR_WM 走 _PIPE 间距 0x1000（与 CUR_* 同） */
    gCurBufCfg = IGPU_REG_CUR_BUF_CFG_A + PipeOff;
    gCurWm0 = IGPU_REG_CUR_WM_A0 + PipeOff;
}

/* 固件已分 DDB 且 WM0 使能才允许开 CUR；否则必黑（underrun） */
static int CursorDdbWmOk(void) {
    UINT32 Cfg;
    UINT32 Wm0;
    UINT32 Start;
    UINT32 End;

    PickCursorRegs();
    if (!IgpuForcewakeGet()) {
        return 0;
    }
    Cfg = IgpuMmioRead32(gCurBufCfg);
    Wm0 = IgpuMmioRead32(gCurWm0);
    Start = Cfg & CUR_BUF_START_MASK;
    End = (Cfg >> CUR_BUF_END_SHIFT) & CUR_BUF_END_MASK;
    if (End <= Start) {
        LogBoot("Boot: igpu hw cursor soft (no ddb) cfg=");
        LogBootHex32(Cfg);
        LogBoot(" wm0=");
        LogBootHex32(Wm0);
        LogBoot("\n");
        return 0;
    }
    if ((Wm0 & CUR_WM_EN) == 0) {
        LogBoot("Boot: igpu hw cursor soft (no wm) cfg=");
        LogBootHex32(Cfg);
        LogBoot(" wm0=");
        LogBootHex32(Wm0);
        LogBoot("\n");
        return 0;
    }
    gSavedBufCfg = Cfg;
    gSavedWm0 = Wm0;
    return 1;
}

static int ProbeCapable(void) {
    if (gProbed) {
        return gCapable;
    }
    gProbed = 1;
    if (!IgpuMmioOk() || !IgpuGsmOk() || !IgpuGttOk()) {
        gCapable = 0;
        return 0;
    }
    /* 无 DDB/WM 视同不支持，避免 Enable 写 CURCNTR 黑屏 */
    if (!CursorDdbWmOk()) {
        gCapable = 0;
        return 0;
    }
    gCapable = 1;
    return 1;
}

static void FailLatch(void) {
    gCapable = 0;
    gOn = 0;
}

static void FillBuiltinArrow(UINT32 *Dst) {
    UINT32 y;
    UINT32 x;
    /* 尖在 (1,1)；白芯黑边，其余透明。glyph 刀换真字形。 */
    static const char *Rows[16] = {
        "X               ",
        "XX              ",
        "X.X             ",
        "X..X            ",
        "X...X           ",
        "X....X          ",
        "X.....X         ",
        "X......X        ",
        "X.......X       ",
        "X........X      ",
        "X.....XXXX      ",
        "X..X..X         ",
        "X.X X.X         ",
        "XX  X.X         ",
        "X    X.X        ",
        "      XX        ",
    };

    for (y = 0; y < IGPU_CUR_SIZE * IGPU_CUR_SIZE; y++) {
        Dst[y] = 0;
    }
    for (y = 0; y < 16u; y++) {
        for (x = 0; x < 16u; x++) {
            char C = Rows[y][x];
            UINT32 Px;

            if (C == ' ') {
                continue;
            }
            if (C == 'X') {
                Px = 0xFF000000u;
            } else {
                Px = 0xFFFFFFFFu;
            }
            Dst[y * IGPU_CUR_SIZE + x] = Px;
        }
    }
}

static void CopyGlyph(const UINT32 *Src, UINT32 W, UINT32 H, UINT32 *Dst) {
    UINT32 y;
    UINT32 x;
    UINT32 Cw;
    UINT32 Ch;

    for (y = 0; y < IGPU_CUR_SIZE * IGPU_CUR_SIZE; y++) {
        Dst[y] = 0;
    }
    if (!Src || W == 0 || H == 0) {
        FillBuiltinArrow(Dst);
        return;
    }
    Cw = W;
    Ch = H;
    if (Cw > IGPU_CUR_SIZE) {
        Cw = IGPU_CUR_SIZE;
    }
    if (Ch > IGPU_CUR_SIZE) {
        Ch = IGPU_CUR_SIZE;
    }
    for (y = 0; y < Ch; y++) {
        for (x = 0; x < Cw; x++) {
            Dst[y * IGPU_CUR_SIZE + x] = Src[y * W + x];
        }
    }
}

static int EnsureBuf(void) {
    UINT64 Flags;

    if (gArgb) {
        return 1;
    }
    gArgb = (UINT32 *)PhysicalMemoryAllocatePages(IGPU_CUR_PAGES);
    if (!gArgb) {
        return 0;
    }
    gPhys = (UINT64)(UINTN)gArgb;
    Flags = HalVideoFbMapFlags();
    if (VirtualMemoryMapRange(gPhys, gPhys, IGPU_CUR_BYTES, Flags) != 0) {
        gArgb = 0;
        gPhys = 0;
        return 0;
    }
    if (!IgpuGsmMap(IGPU_CUR_GTT_OFF, gPhys)) {
        gArgb = 0;
        gPhys = 0;
        return 0;
    }
    return 1;
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

/* i915：POS 后必写 BASE 武装；任一 CUR* 写会取消未武装更新 */
static void ArmCursor(UINT32 Ctl, UINT32 Pos) {
    IgpuMmioWrite32(gCurBufCfg, gSavedBufCfg);
    IgpuMmioWrite32(gCurWm0, gSavedWm0);
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
    /* 再读一次：Present/翻页后 DDB 仍须在 */
    if (!CursorDdbWmOk()) {
        FailLatch();
        return 0;
    }
    if (gOn) {
        HalVideoHwCursorDisable();
    }
    if (!EnsureBuf()) {
        FailLatch();
        LogBoot("Boot: igpu hw cursor fail (buf)\n");
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
        LogBoot("Boot: igpu hw cursor fail (fw)\n");
        return 0;
    }

    ArmCursor(MCURSOR_MODE_64_ARGB_AX, EncodePos(0, 0));

    gOn = 1;
    LogBoot("Boot: igpu hw cursor ok cfg=");
    LogBootHex32(gSavedBufCfg);
    LogBoot("\n");
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
