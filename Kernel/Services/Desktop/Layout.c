/*
 * Layout.c — 图标网格容量与列换行（缩放 150%/200% 不再叠）
 *
 * 关键口径：位置、图标、标签都是逻辑单位；绘制层再按 DrawNum/Den 换算。
 * 行高必须容纳「图标 + 标签 + 行距」，逻辑屏变矮时不能把多行硬钳到
 * ClampIconPos 的 MaxY（那会把不同行压成同一 y）。做法：容量不够时按
 * 列换行，列宽取该列最大（图标、标签）宽度 + 间距，标签不会伸进下一列。
 *
 * 100% 下原布局（含 BLOCKS.DB 里拖过的坐标）仍然 fit，本文件不改它。
 */
#include "DesktopPrivate.h"

/* 1=重排后仍放不下（连列宽都溢出）；探针会打 layout=min-overlap，不装 none */
static int sIconLayoutLimited;

static UINT32 LayoutLabelW(int Idx) {
    return gIcons[Idx].Label ? FontStringWidth(gIcons[Idx].Label) : 0u;
}

static UINT32 LayoutCellH(void) {
    return DESKTOP_ICON_SIZE + DESKTOP_LABEL_PAD + FontCellH() +
           DESKTOP_ICON_GAP;
}

/* 当前逻辑高度能完整放下几行（最后一行标签底边须在任务栏上方留边） */
static UINT32 LayoutRowsFit(void) {
    UINT32 BarY;
    UINT32 Sw;
    UINT32 Sh;
    UINT32 Bottom;
    UINT32 CellH = LayoutCellH();

    TaskbarGeom(&BarY, &Sw, &Sh);
    Bottom = DESKTOP_ORIGIN_Y + DESKTOP_ICON_SIZE + DESKTOP_LABEL_PAD +
             FontCellH();
    if (BarY <= Bottom + DESKTOP_EDGE_MARGIN || CellH == 0u) {
        return 1u;
    }
    return (BarY - DESKTOP_EDGE_MARGIN - Bottom) / CellH + 1u;
}

/* 图标脸与标签的逻辑矩形；标签宽为 0 表示无标签 */
static void LayoutRects(int Idx, UINT32 *Fx, UINT32 *Fy, UINT32 *Fw,
                        UINT32 *Fh, UINT32 *Lx, UINT32 *Ly, UINT32 *Lw,
                        UINT32 *Lh) {
    const DESKTOP_ICON *Icon = &gIcons[Idx];
    UINT32 W = LayoutLabelW(Idx);
    UINT32 X = Icon->X;

    if (W > 0u && W < DESKTOP_ICON_SIZE) {
        X = Icon->X + (DESKTOP_ICON_SIZE - W) / 2u;
    }
    *Fx = Icon->X;
    *Fy = Icon->Y;
    *Fw = DESKTOP_ICON_SIZE;
    *Fh = DESKTOP_ICON_SIZE;
    *Lx = X;
    *Ly = Icon->Y + DESKTOP_ICON_SIZE + DESKTOP_LABEL_PAD;
    *Lw = W;
    *Lh = FontCellH();
}

/*
 * 四判据的逻辑版：图标相交 / 标签压图标（含自己）/ 标签压其它图标 /
 * 压任务栏或出屏。逻辑不相交在 100%、150%、200% 下都不会物理相交
 * （比例是整数有理数，缩放单调）。
 */
int DesktopIconLayoutFits(void) {
    UINT32 Sw;
    UINT32 Sh;
    UINT32 BarY;
    int i;
    int j;

    TaskbarGeom(&BarY, &Sw, &Sh);
    for (i = 0; i < DESKTOP_ICON_COUNT; i++) {
        UINT32 Fx;
        UINT32 Fy;
        UINT32 Fw;
        UINT32 Fh;
        UINT32 Lx;
        UINT32 Ly;
        UINT32 Lw;
        UINT32 Lh;

        if (!gIcons[i].Present) {
            continue;
        }
        LayoutRects(i, &Fx, &Fy, &Fw, &Fh, &Lx, &Ly, &Lw, &Lh);
        if (Fx + Fw > Sw || Fy + Fh > BarY || Lx + Lw > Sw ||
            Ly + Lh > BarY) {
            return 0;
        }
        if (Lw != 0u && RectsOverlap(Fx, Fy, Fw, Fh, Lx, Ly, Lw, Lh)) {
            return 0;
        }
        for (j = 0; j < DESKTOP_ICON_COUNT; j++) {
            UINT32 Gx;
            UINT32 Gy;
            UINT32 Gw;
            UINT32 Gh;
            UINT32 Mx;
            UINT32 My;
            UINT32 Mw;
            UINT32 Mh;

            if (j == i || !gIcons[j].Present) {
                continue;
            }
            LayoutRects(j, &Gx, &Gy, &Gw, &Gh, &Mx, &My, &Mw, &Mh);
            if (RectsOverlap(Fx, Fy, Fw, Fh, Gx, Gy, Gw, Gh)) {
                return 0;
            }
            if (Lw != 0u && RectsOverlap(Lx, Ly, Lw, Lh, Gx, Gy, Gw, Gh)) {
                return 0;
            }
        }
    }
    return 1;
}

/* 按槽位序号收集：系统图标在前，应用图标在后 */
static int LayoutCollect(int *Items, int *SysN) {
    int N = 0;
    int i;

    for (i = 0; i < DESKTOP_SYS_ICON_COUNT; i++) {
        if (gIcons[i].Present) {
            Items[N++] = i;
        }
    }
    *SysN = N;
    for (i = DESKTOP_SYS_ICON_COUNT; i < DESKTOP_ICON_COUNT; i++) {
        if (gIcons[i].Present) {
            Items[N++] = i;
        }
    }
    return N;
}

/* 组内按列优先填格：先竖着填满 Rows 行，再换下一列 */
static void LayoutAssignGroup(int First, int Count, UINT32 Rows, int BaseCol,
                              int *Cols, int *RowsOut) {
    int k;

    for (k = 0; k < Count; k++) {
        Cols[First + k] = BaseCol + k / (int)Rows;
        RowsOut[First + k] = k % (int)Rows;
    }
}

void DesktopReflowIcons(void) {
    int Items[DESKTOP_ICON_COUNT];
    int Cols[DESKTOP_ICON_COUNT];
    int RowsIdx[DESKTOP_ICON_COUNT];
    UINT32 Width[DESKTOP_ICON_COUNT] = {0};
    UINT32 ColX[DESKTOP_ICON_COUNT] = {0};
    UINT32 Rows = LayoutRowsFit();
    UINT32 CellH = LayoutCellH();
    int SysN = 0;
    int N;
    int i;
    int C;
    int ColsN = 0;

    N = LayoutCollect(Items, &SysN);
    if (Rows < 1u) {
        Rows = 1u;
    }
    LayoutAssignGroup(0, SysN, Rows, 0, Cols, RowsIdx);
    LayoutAssignGroup(SysN, N - SysN, Rows,
                      (SysN + (int)Rows - 1) / (int)Rows, Cols, RowsIdx);
    for (i = 0; i < N; i++) {
        UINT32 Need = DESKTOP_ICON_SIZE;
        UINT32 Lw = LayoutLabelW(Items[i]);

        if (Lw > Need) {
            Need = Lw;
        }
        Need += DESKTOP_ICON_GAP;
        C = Cols[i];
        if (C + 1 > ColsN) {
            ColsN = C + 1;
        }
        if (C >= 0 && C < DESKTOP_ICON_COUNT && Need > Width[C]) {
            Width[C] = Need;
        }
    }
    if (ColsN > 0) {
        ColX[0] = DESKTOP_ORIGIN_X;
        for (C = 1; C < ColsN; C++) {
            ColX[C] = ColX[C - 1] + Width[C - 1];
        }
    }
    for (i = 0; i < N; i++) {
        int Idx = Items[i];

        gIcons[Idx].X = ColX[Cols[i]];
        gIcons[Idx].Y = DESKTOP_ORIGIN_Y + (UINT32)RowsIdx[i] * CellH;
    }
}

void DesktopEnsureIconLayout(void) {
    if (!DesktopIconLayoutFits()) {
        DesktopReflowIcons();
        sIconLayoutLimited = DesktopIconLayoutFits() ? 0 : 1;
    } else {
        sIconLayoutLimited = 0;
    }
}

int DesktopIconLayoutLimited(void) {
    return sIconLayoutLimited;
}

/* 缩放/分辨率热切：先回到最近一次保存的格子，再按新容量判断是否要换行 */
void DesktopRestoreSavedLayout(void) {
    int i;

    for (i = 0; i < DESKTOP_ICON_COUNT; i++) {
        if (!gIcons[i].Present || !gIcons[i].SavedValid) {
            continue;
        }
        gIcons[i].X = gIcons[i].SavedX;
        gIcons[i].Y = gIcons[i].SavedY;
    }
}

/* 拖放松手 / 默认布局后：记住可回退的格子（跨缩放热切用） */
void DesktopRememberIconPos(int Idx) {
    if (Idx < 0 || Idx >= DESKTOP_ICON_COUNT || !gIcons[Idx].Present) {
        return;
    }
    gIcons[Idx].SavedX = gIcons[Idx].X;
    gIcons[Idx].SavedY = gIcons[Idx].Y;
    gIcons[Idx].SavedValid = 1;
}
