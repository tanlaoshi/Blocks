/*
 * IgpuHwCursorDdb.c — CUR DDB/WM 探测与从 primary 尾割块
 *
 * 自 IgpuHwCursor.c 纯搬。开 CUR 前必须有 CUR_BUF_CFG + CUR_WM_EN。
 */
#include "IgpuHwCursorPrivate.h"
#include "Igpu.h"
#include "SerialLog.h"

static void PickCursorRegs(void) {
    UINT32 SurfReg;
    UINT32 PipeOff;

    SurfReg = IgpuGttSurfRegister();
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
    gCurrentBufferConfig = IGPU_REG_CUR_BUF_CFG_A + PipeOff;
    gCurWm0 = IGPU_REG_CUR_WM_A0 + PipeOff;
    gPlaneBufReg = IGPU_REG_PLANE_BUF_1_A + PipeOff;
    gPlaneWm0Reg = IGPU_REG_PLANE_WM_1_A0 + PipeOff;
}

static void ArmPlaneSurf(void) {
    UINT32 SurfReg;
    UINT32 Surf;

    SurfReg = IgpuGttSurfRegister();
    Surf = IgpuMmioRead32(SurfReg);
    IgpuMmioWrite32(SurfReg, Surf);
}

static int CursorDdbWmOkQuiet(void) {
    UINT32 Cfg;
    UINT32 Wm0;
    UINT32 Start;
    UINT32 End;

    Cfg = IgpuMmioRead32(gCurrentBufferConfig);
    Wm0 = IgpuMmioRead32(gCurWm0);
    Start = Cfg & BUF_START_MASK;
    End = (Cfg >> BUF_END_SHIFT) & BUF_END_MASK;
    if (End <= Start) {
        return 0;
    }
    if ((Wm0 & CUR_WM_EN) == 0) {
        return 0;
    }
    gSavedBufferConfig = Cfg;
    gSavedWm0 = Wm0;
    return 1;
}

/*
 * 从 primary 尾割 IGPU_CUR_DDB_BLOCKS；剩余须能盖住 primary WM0 blocks。
 * CUR_WM：固件已有值 | EN（NUC 曾见 wm0=0x4007）；blocks 超分配则软退。
 */
static int CarveCursorDdb(void) {
    UINT32 PlaneConfig;
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
    UINT32 NewPlaneConfig;

    PlaneConfig = IgpuMmioRead32(gPlaneBufReg);
    PlaneWm = IgpuMmioRead32(gPlaneWm0Reg);
    CurWm = IgpuMmioRead32(gCurWm0);
    PStart = PlaneConfig & BUF_START_MASK;
    PEnd = (PlaneConfig >> BUF_END_SHIFT) & BUF_END_MASK;
    if (PEnd <= PStart) {
        LogDrv("Boot: igpu hw cursor soft (plane ddb empty) cfg=");
        LogDrvHex32(PlaneConfig);
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
    NewPlaneConfig = (NewPEnd << BUF_END_SHIFT) | PStart;
    gOrigPlaneBuf = PlaneConfig;
    gSavedBufferConfig = (CEnd << BUF_END_SHIFT) | CStart;
    gSavedWm0 = CurWm | CUR_WM_EN;

    IgpuMmioWrite32(gPlaneBufReg, NewPlaneConfig);
    ArmPlaneSurf();
    IgpuMmioWrite32(gCurrentBufferConfig, gSavedBufferConfig);
    IgpuMmioWrite32(gCurWm0, gSavedWm0);
    /* CUR 尚未 Enable；BASE 武装留到 Enable / SetPos */
    IgpuMmioWrite32(gCurFbc, 0);
    IgpuMmioWrite32(gCurCtl, 0);
    IgpuMmioWrite32(gCurBase, (UINT32)IGPU_CUR_GTT_OFF);

    gCarved = 1;
    LogDrv("Boot: igpu hw cursor ddb carve plane=");
    LogDrvHex32(PlaneConfig);
    LogDrv("->");
    LogDrvHex32(NewPlaneConfig);
    LogDrv(" cur=");
    LogDrvHex32(gSavedBufferConfig);
    LogDrv(" wm0=");
    LogDrvHex32(gSavedWm0);
    LogDrv("\n");
    return 1;
}

int EnsureCursorDdb(void) {
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

void RestorePlaneDdb(void) {
    if (!gCarved) {
        return;
    }
    if (!IgpuForcewakeGet()) {
        return;
    }
    IgpuMmioWrite32(gCurFbc, 0);
    IgpuMmioWrite32(gCurCtl, 0);
    IgpuMmioWrite32(gCurrentBufferConfig, 0);
    IgpuMmioWrite32(gCurWm0, gSavedWm0 & ~CUR_WM_EN);
    IgpuMmioWrite32(gCurBase, (UINT32)IGPU_CUR_GTT_OFF);
    IgpuMmioWrite32(gPlaneBufReg, gOrigPlaneBuf);
    ArmPlaneSurf();
    gCarved = 0;
    gDdbReady = 0;
}
