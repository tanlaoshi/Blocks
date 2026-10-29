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
 */
#include "Igpu.h"
#include "IgpuBlitPrivate.h"
#include "HalVideo.h"
#include "PhysicalMemory.h"
#include "VirtualMemory.h"
#include "SerialLog.h"

#define IGPU_CUR_GTT_OFF       0x05900000ull
#define IGPU_CUR_SIZE          64u
#define IGPU_CUR_BYTES         (IGPU_CUR_SIZE * IGPU_CUR_SIZE * 4u)
#define IGPU_CUR_PAGES         ((IGPU_CUR_BYTES + 4095u) / 4096u)
/* i915 skl_cursor_allocation：单 active pipe 至少 32 blocks */
#define IGPU_CUR_DDB_BLOCKS    32u

#define IGPU_REG_CUR_CTL_A     0x70080u
#define IGPU_REG_CUR_BASE_A    0x70084u
#define IGPU_REG_CUR_POS_A     0x70088u
#define IGPU_REG_CUR_FBC_CTL_A 0x700A0u
#define IGPU_REG_CUR_BUF_CFG_A 0x7017Cu
#define IGPU_REG_CUR_WM_A0     0x70140u
/* PLANE_1（primary）BUF/WM；与 PLANE_CTL 0x70180 同管 */
#define IGPU_REG_PLANE_BUF_1_A 0x7027Cu
#define IGPU_REG_PLANE_WM_1_A0 0x70240u
#define IGPU_REG_PLANE_SURF_B  0x7119Cu
#define IGPU_REG_PLANE_SURF_C  0x7219Cu

#define MCURSOR_MODE_64_ARGB_AX 0x27u
#define CURSOR_POS_X_SIGN      (1u << 15)
#define CURSOR_POS_Y_SIGN      (1u << 31)
#define CUR_WM_EN              (1u << 31)
#define PLANE_WM_EN            (1u << 31)
#define PLANE_WM_BLOCKS_MASK   0x1FFFu
#define CUR_WM_BLOCKS_MASK     0xFFFu
#define BUF_START_MASK         0xFFFu
#define BUF_END_SHIFT          16
#define BUF_END_MASK           0xFFFu

static int gCapable;
static int gProbed;
static int gOn;
static int gDdbReady;
static UINT32 *gArgb;
static UINT64 gPhys;
static UINT32 gHotX;
static UINT32 gHotY;
static UINT32 gCurCtl;
static UINT32 gCurBase;
static UINT32 gCurPos;
static UINT32 gCurFbc;
static UINT32 gCurBufCfg;
static UINT32 gCurWm0;
static UINT32 gPlaneBufReg;
static UINT32 gPlaneWm0Reg;
static UINT32 gSavedBufCfg;
static UINT32 gSavedWm0;
static UINT32 gOrigPlaneBuf;
static int gCarved;

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
    gCurFbc = IGPU_REG_CUR_FBC_CTL_A + PipeOff;
    gCurBufCfg = IGPU_REG_CUR_BUF_CFG_A + PipeOff;
    gCurWm0 = IGPU_REG_CUR_WM_A0 + PipeOff;
    gPlaneBufReg = IGPU_REG_PLANE_BUF_1_A + PipeOff;
    gPlaneWm0Reg = IGPU_REG_PLANE_WM_1_A0 + PipeOff;
}

static void ArmPlaneSurf(void) {
    UINT32 SurfReg;
    UINT32 Surf;

    SurfReg = IgpuGttSurfReg();
    Surf = IgpuMmioRead32(SurfReg);
    IgpuMmioWrite32(SurfReg, Surf);
}

static int CursorDdbWmOkQuiet(void) {
    UINT32 Cfg;
    UINT32 Wm0;
    UINT32 Start;
    UINT32 End;

    Cfg = IgpuMmioRead32(gCurBufCfg);
    Wm0 = IgpuMmioRead32(gCurWm0);
    Start = Cfg & BUF_START_MASK;
    End = (Cfg >> BUF_END_SHIFT) & BUF_END_MASK;
    if (End <= Start) {
        return 0;
    }
    if ((Wm0 & CUR_WM_EN) == 0) {
        return 0;
    }
    gSavedBufCfg = Cfg;
    gSavedWm0 = Wm0;
    return 1;
}

/*
 * 从 primary 尾割 IGPU_CUR_DDB_BLOCKS；剩余须能盖住 primary WM0 blocks。
 * CUR_WM：固件已有值 | EN（NUC 曾见 wm0=0x4007）；blocks 超分配则软退。
 */
static int CarveCursorDdb(void) {
    UINT32 PlaneCfg;
    UINT32 PlaneWm;
    UINT32 CurWm;
    UINT32 PStart;
    UINT32 PEnd;
    UINT32 PSize;
    UINT32 PWmBlocks;
    UINT32 CBlocks;
    UINT32 NewPEnd;
    UINT32 CStart;
    UINT32 CEnd;
    UINT32 NewPlaneCfg;

    PlaneCfg = IgpuMmioRead32(gPlaneBufReg);
    PlaneWm = IgpuMmioRead32(gPlaneWm0Reg);
    CurWm = IgpuMmioRead32(gCurWm0);
    PStart = PlaneCfg & BUF_START_MASK;
    PEnd = (PlaneCfg >> BUF_END_SHIFT) & BUF_END_MASK;
    if (PEnd <= PStart) {
        LogDrv("Boot: igpu hw cursor soft (plane ddb empty) cfg=");
        LogDrvHex32(PlaneCfg);
        LogDrv("\n");
        return 0;
    }
    PSize = PEnd - PStart + 1u;
    PWmBlocks = PlaneWm & PLANE_WM_BLOCKS_MASK;
    if ((PlaneWm & PLANE_WM_EN) != 0 && PWmBlocks >= PSize) {
        LogDrv("Boot: igpu hw cursor soft (plane wm>=ddb) wm=");
        LogDrvHex32(PlaneWm);
        LogDrv("\n");
        return 0;
    }
    if (PSize < PWmBlocks + IGPU_CUR_DDB_BLOCKS + 1u) {
        LogDrv("Boot: igpu hw cursor soft (ddb tight) psz=");
        LogDrvHex32(PSize);
        LogDrv(" wmb=");
        LogDrvHex32(PWmBlocks);
        LogDrv("\n");
        return 0;
    }

    CBlocks = CurWm & CUR_WM_BLOCKS_MASK;
    if (CBlocks == 0 || CBlocks >= IGPU_CUR_DDB_BLOCKS) {
        /* 固件无可用 cursor WM blocks：软退，勿臆造 lines/blocks */
        LogDrv("Boot: igpu hw cursor soft (cur wm) wm0=");
        LogDrvHex32(CurWm);
        LogDrv("\n");
        return 0;
    }

    NewPEnd = PEnd - IGPU_CUR_DDB_BLOCKS;
    CStart = NewPEnd + 1u;
    CEnd = PEnd;
    NewPlaneCfg = (NewPEnd << BUF_END_SHIFT) | PStart;
    gOrigPlaneBuf = PlaneCfg;
    gSavedBufCfg = (CEnd << BUF_END_SHIFT) | CStart;
    gSavedWm0 = CurWm | CUR_WM_EN;

    IgpuMmioWrite32(gPlaneBufReg, NewPlaneCfg);
    ArmPlaneSurf();
    IgpuMmioWrite32(gCurBufCfg, gSavedBufCfg);
    IgpuMmioWrite32(gCurWm0, gSavedWm0);
    /* CUR 尚未 Enable；BASE 武装留到 Enable / SetPos */
    IgpuMmioWrite32(gCurFbc, 0);
    IgpuMmioWrite32(gCurCtl, 0);
    IgpuMmioWrite32(gCurBase, (UINT32)IGPU_CUR_GTT_OFF);

    gCarved = 1;
    LogDrv("Boot: igpu hw cursor ddb carve plane=");
    LogDrvHex32(PlaneCfg);
    LogDrv("->");
    LogDrvHex32(NewPlaneCfg);
    LogDrv(" cur=");
    LogDrvHex32(gSavedBufCfg);
    LogDrv(" wm0=");
    LogDrvHex32(gSavedWm0);
    LogDrv("\n");
    return 1;
}

static int EnsureCursorDdb(void) {
    PickCursorRegs();
    if (!IgpuForcewakeGet()) {
        return 0;
    }
    if (gDdbReady) {
        return 1;
    }
    if (CursorDdbWmOkQuiet()) {
        gDdbReady = 1;
        return 1;
    }
    if (!CarveCursorDdb()) {
        return 0;
    }
    if (!CursorDdbWmOkQuiet()) {
        LogDrv("Boot: igpu hw cursor soft (carve verify)\n");
        return 0;
    }
    gDdbReady = 1;
    return 1;
}

static void RestorePlaneDdb(void) {
    if (!gCarved) {
        return;
    }
    if (!IgpuForcewakeGet()) {
        return;
    }
    IgpuMmioWrite32(gCurFbc, 0);
    IgpuMmioWrite32(gCurCtl, 0);
    IgpuMmioWrite32(gCurBufCfg, 0);
    IgpuMmioWrite32(gCurWm0, gSavedWm0 & ~CUR_WM_EN);
    IgpuMmioWrite32(gCurBase, (UINT32)IGPU_CUR_GTT_OFF);
    IgpuMmioWrite32(gPlaneBufReg, gOrigPlaneBuf);
    ArmPlaneSurf();
    gCarved = 0;
    gDdbReady = 0;
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

static void FillBuiltinArrow(UINT32 *Dst) {
    UINT32 y;
    UINT32 x;
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
    UINT32 i;

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
    /*
     * 光标位图是 64×64×4=16KB，横跨 4 个 4KB GTT 页；IgpuGsmMap() 一次只装
     * 一个 PTE。旧代码只映第一页 → 硬件只看到前 16 行：100% 的 12 行箭头
     * 恰好完整，150% 缺尾巴，200% 只剩左上角。必须逐页映射再统一 flush。
     */
    for (i = 0; i < IGPU_CUR_PAGES; i++) {
        UINT64 Off = IGPU_CUR_GTT_OFF + (UINT64)i * 4096ull;

        if (!IgpuGsmMapQuiet(Off, gPhys + (UINT64)i * 4096ull)) {
            gArgb = 0;
            gPhys = 0;
            return 0;
        }
    }
    IgpuGsmFlush();
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

static void ArmCursor(UINT32 Ctl, UINT32 Pos) {
    IgpuMmioWrite32(gCurBufCfg, gSavedBufCfg);
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
    if (!EnsureBuf()) {
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
    LogDrvHex32(gSavedBufCfg);
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
