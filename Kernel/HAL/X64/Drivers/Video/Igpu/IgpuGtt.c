/*
 * IgpuGtt.c — PR-G-igpu-2/3：观察固件 scanout（不写 GGTT PTE）
 *
 * NUC：PLANE_CTL 已开而 SURF=0 → GGTT 偏移 0（固件恒等映）仍可复用。
 */
#include "Igpu.h"
#include "HalVideo.h"
#include "SerialLog.h"

#define IGPU_REG_PLANE_SURF_A  0x7019Cu
#define IGPU_REG_PLANE_SURF_B  0x7119Cu
#define IGPU_REG_PLANE_SURF_C  0x7219Cu
#define IGPU_REG_PLANE_CTL_A   0x70180u
#define IGPU_REG_PLANE_CTL_B   0x71180u
#define IGPU_REG_PLANE_CTL_C   0x72180u
#define IGPU_PLANE_CTL_ENABLE  0x80000000u

static int gIgpuGttOk;
static UINT32 gIgpuSurf;
static UINT32 gIgpuSurfReg = IGPU_REG_PLANE_SURF_A;
static UINT64 gIgpuFbPhys;

int IgpuGttOk(void) {
    return gIgpuGttOk;
}

UINT32 IgpuGttSurf(void) {
    return gIgpuSurf;
}

UINT32 IgpuGttSurfReg(void) {
    return gIgpuSurfReg;
}

static UINT32 PickSurf(UINT32 *CtlOut) {
    UINT32 Surf[3];
    UINT32 Ctl[3];
    static const UINT32 SurfReg[3] = {
        IGPU_REG_PLANE_SURF_A, IGPU_REG_PLANE_SURF_B, IGPU_REG_PLANE_SURF_C
    };
    int i;

    Surf[0] = IgpuMmioRead32(IGPU_REG_PLANE_SURF_A);
    Surf[1] = IgpuMmioRead32(IGPU_REG_PLANE_SURF_B);
    Surf[2] = IgpuMmioRead32(IGPU_REG_PLANE_SURF_C);
    Ctl[0] = IgpuMmioRead32(IGPU_REG_PLANE_CTL_A);
    Ctl[1] = IgpuMmioRead32(IGPU_REG_PLANE_CTL_B);
    Ctl[2] = IgpuMmioRead32(IGPU_REG_PLANE_CTL_C);

    for (i = 0; i < 3; i++) {
        if ((Ctl[i] & IGPU_PLANE_CTL_ENABLE) == 0) {
            continue;
        }
        if (Surf[i] == 0xFFFFFFFFu) {
            continue;
        }
        if ((Surf[i] & 0xFFFu) != 0) {
            continue;
        }
        if (CtlOut) {
            *CtlOut = Ctl[i];
        }
        gIgpuSurfReg = SurfReg[i];
        return Surf[i]; /* 可为 0 = GGTT 根 */
    }
    if (CtlOut) {
        *CtlOut = Ctl[0];
    }
    return 0xFFFFFFFFu; /* 无启用 plane */
}

int IgpuGttInit(void) {
    UINT32 Surf;
    UINT32 Ctl;
    UINT64 Fb;

    if (gIgpuGttOk) {
        return 1;
    }
    if (!IgpuMmioOk()) {
        return 0;
    }

    (void)IgpuForcewakeGet();

    Fb = HalVideoFrameBufferBase();
    gIgpuFbPhys = Fb;
    Surf = PickSurf(&Ctl);
    if (Surf == 0xFFFFFFFFu) {
        LogDrv("Boot: igpu gtt soft (no plane) ctl=");
        LogDrvHex32(Ctl);
        LogDrv("\n");
        return 0;
    }
    gIgpuSurf = Surf;

    gIgpuGttOk = 1;
    LogDrv("Boot: igpu gtt ok surf=");
    LogDrvHex32(Surf);
    LogDrv(" ctl=");
    LogDrvHex32(Ctl);
    LogDrv(" fb=");
    LogDrvHex32((UINT32)Fb);
    if (Fb > 0xFFFFFFFFu) {
        LogDrv(":");
        LogDrvHex32((UINT32)(Fb >> 32));
    }
    if (Surf == 0) {
        LogDrv(" (gtt0)");
    }
    LogDrv("\n");
    return 1;
}
