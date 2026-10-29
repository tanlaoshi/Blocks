/*
 * GuiCursorShape.c — 光标字形：指针箭头 / ↔ / ↕ / 对角 resize
 *
 * 热点：箭头 = 尖端；resize = 字形中心。
 * hw-cursor-glyph：CursorGlyphToArgb 栅进 64×64 ARGB（CUR 平面）。
 */
#include "GuiPrivate.h"
#include "HalVideo.h"
#include "UI.h"

static UINT32 *gGlyphBuf;
static int gGlyphOrigX;
static int gGlyphOrigY;
static int gGlyphHotX;
static int gGlyphHotY;
/* 逻辑像素 → 硬件光标位图像素的比例（Num/Den，与绘制变换同源） */
static UINT32 gGlyphNum = 1;
static UINT32 gGlyphDen = 1;
/*
 * 硬件字形按「物理屏高」取 100% 几何再乘 Num/Den：缩放档只改逻辑屏高，
 * 若直接按逻辑屏高取几何，150%/200% 的 Head/Stem 会被最小值夹住，物理
 * 尾巴只长 1.0/1.33 倍而不是 1.5/2 倍（真机表现为「少一点尾巴」）。
 * 0 = 按当前逻辑屏高（软件光标路径）。
 */
static UINT32 gCursorMetricH;

/* 向下取整除法（C 的整数除法向 0 截断，负坐标会差一像素） */
static int GlyphFloorDiv(int V, UINT32 Den) {
    if (V >= 0) {
        return (int)((UINT32)V / Den);
    }
    return -(int)(((UINT32)(-V) + Den - 1u) / Den);
}

/*
 * 逻辑负边界 → 位图热点：ceil(-Min * Num / Den)。
 * 取 floor 会把箭头/描边最左一列裁掉（150% 下 -3 逻辑 = -4.5 物理）。
 */
static int GlyphHotFromLogical(int Min) {
    if (Min >= 0) {
        return 0;
    }
    return (int)(((UINT32)(-Min) * gGlyphNum + gGlyphDen - 1u) / gGlyphDen);
}

/* 向上取整除法：物理块右/下端点必须用 ceil，floor 会少最后一行/列 */
static int GlyphCeilDiv(int V, UINT32 Den) {
    return GlyphFloorDiv(V + (int)Den - 1, Den);
}

/* 逻辑右/下端点 → 位图内端点（ceil，块覆盖 [start, end)） */
static int GlyphEndFromLogical(int Max) {
    return GlyphCeilDiv((Max + 1) * (int)gGlyphNum, gGlyphDen);
}

void CursorMetrics(int *Half, int *Thick) {
    UINT32 H;
    UINT32 HalfU;
    int T;

    H = gCursorMetricH != 0 ? gCursorMetricH :
        (gGuiWindowManager.ScreenHeight != 0 ? gGuiWindowManager.ScreenHeight : CURSOR_REF_H);
    HalfU = (CURSOR_HALF_BASE * H + CURSOR_REF_H / 2u) / CURSOR_REF_H;
    if (HalfU < 4u) {
        HalfU = 4u;
    }
    if (HalfU > (UINT32)CURSOR_HALF_MAX) {
        HalfU = (UINT32)CURSOR_HALF_MAX;
    }
    *Half = (int)HalfU;
    T = (int)HalfU / CURSOR_HALF_BASE;
    if (T > 0) {
        T--;
    }
    if (T > CURSOR_THICK_MAX) {
        T = CURSOR_THICK_MAX;
    }
    *Thick = T;
}

void CursorPutPixel(int Px, int Py, UINT32 Color) {
    if (gGlyphBuf) {
        int RelX;
        int RelY;
        int Rx0;
        int Rx1;
        int Ry0;
        int Ry1;
        int Rx;
        int Ry;
        UINT32 C = 0xFF000000u | (Color & 0x00FFFFFFu);

        RelX = Px - gGlyphOrigX;
        RelY = Py - gGlyphOrigY;
        /*
         * 硬件光标平面是物理 1:1。一个逻辑像素必须写成
         * [floor(r*Num/Den), floor((r+1)*Num/Den)) 的实心块；旧代码只写
         * 一个点，200% 下隔行隔列全是透明 → 光标成网格/三角形，150% 下
         * 半格错位成菱形。宽度用端点相减，与软件路径 PixelSpan 同规则。
         */
        Rx0 = GlyphFloorDiv(RelX * (int)gGlyphNum, gGlyphDen) + gGlyphHotX;
        Rx1 = GlyphCeilDiv((RelX + 1) * (int)gGlyphNum, gGlyphDen) + gGlyphHotX;
        Ry0 = GlyphFloorDiv(RelY * (int)gGlyphNum, gGlyphDen) + gGlyphHotY;
        Ry1 = GlyphCeilDiv((RelY + 1) * (int)gGlyphNum, gGlyphDen) + gGlyphHotY;
        if (Rx1 <= Rx0) {
            Rx1 = Rx0 + 1;
        }
        if (Ry1 <= Ry0) {
            Ry1 = Ry0 + 1;
        }
        if (Rx0 < 0) {
            Rx0 = 0;
        }
        if (Ry0 < 0) {
            Ry0 = 0;
        }
        if (Rx1 > CURSOR_HW_DIM) {
            Rx1 = CURSOR_HW_DIM;
        }
        if (Ry1 > CURSOR_HW_DIM) {
            Ry1 = CURSOR_HW_DIM;
        }
        for (Ry = Ry0; Ry < Ry1; Ry++) {
            UINT32 *Row = &gGlyphBuf[(UINT32)Ry * CURSOR_HW_DIM];

            for (Rx = Rx0; Rx < Rx1; Rx++) {
                Row[Rx] = C;
            }
        }
        return;
    }
    if (Px < 0 || Py < 0) {
        return;
    }
    if ((UINT32)Px >= gGuiWindowManager.ScreenWidth ||
        (UINT32)Py >= gGuiWindowManager.ScreenHeight) {
        return;
    }
    HalVideoDrawPixelRaw((UINT32)Px, (UINT32)Py, Color);
}

/*
 * 指针：等腰三角（尖=热点）+ 沿对称轴伸出的尾巴。
 * 轴 (1,1) 正右下 45°，底边用垂直向量 (-1,1) 对称展开，避免 (1,2) 那般左下歪。
 */
static int Orient2(int Ax, int Ay, int Bx, int By, int Cx, int Cy) {
    return (Bx - Ax) * (Cy - Ay) - (By - Ay) * (Cx - Ax);
}

static int InTriangle(int Px, int Py, int X0, int Y0, int X1, int Y1, int X2,
                      int Y2) {
    int O1 = Orient2(X0, Y0, X1, Y1, Px, Py);
    int O2 = Orient2(X1, Y1, X2, Y2, Px, Py);
    int O3 = Orient2(X2, Y2, X0, Y0, Px, Py);
    int Neg = (O1 < 0) || (O2 < 0) || (O3 < 0);
    int Pos = (O1 > 0) || (O2 > 0) || (O3 > 0);

    return !(Neg && Pos);
}

/* 点到线段距离² ≤ R² */
static int NearSeg(int Px, int Py, int X0, int Y0, int X1, int Y1, int R) {
    int Dx = X1 - X0;
    int Dy = Y1 - Y0;
    int L2 = Dx * Dx + Dy * Dy;
    int T;
    int Qx;
    int Qy;
    int Ex;
    int Ey;

    if (L2 <= 0) {
        Ex = Px - X0;
        Ey = Py - Y0;
        return Ex * Ex + Ey * Ey <= R * R;
    }
    T = ((Px - X0) * Dx + (Py - Y0) * Dy);
    if (T < 0) {
        T = 0;
    }
    if (T > L2) {
        T = L2;
    }
    Qx = X0 + (Dx * T) / L2;
    Qy = Y0 + (Dy * T) / L2;
    Ex = Px - Qx;
    Ey = Py - Qy;
    return Ex * Ex + Ey * Ey <= R * R;
}

static void ArrowGeom(int *TipX, int *TipY, int *Lx, int *Ly, int *Rx, int *Ry,
                      int *Bx, int *By, int *Ex, int *Ey, int *StemR) {
    int Half;
    int Thick;
    int Head;
    int Stem;
    int Wing;

    CursorMetrics(&Half, &Thick);
    /* 三角高度再矮一点；翼仍窄保细尖；尾短细 */
    Head = Half;
    if (Head < 6) {
        Head = 6;
    }
    Stem = Half / 2; /* 再短 2px（原 Half/2+2） */
    if (Stem < 2) {
        Stem = 2;
    }
    Wing = Half / 2;
    if (Wing < 2) {
        Wing = 2;
    }
    *TipX = 0;
    *TipY = 0;
    *Bx = Head;
    *By = Head;
    *Lx = *Bx - Wing;
    *Ly = *By + Wing;
    *Rx = *Bx + Wing;
    *Ry = *By - Wing;
    *Ex = *Bx + Stem;
    *Ey = *By + Stem;
    *StemR = (Thick > 0) ? Thick : 1;
}

static int ArrowSolid(int Ox, int Oy) {
    int TipX;
    int TipY;
    int Lx;
    int Ly;
    int Rx;
    int Ry;
    int Bx;
    int By;
    int Ex;
    int Ey;
    int StemR;

    ArrowGeom(&TipX, &TipY, &Lx, &Ly, &Rx, &Ry, &Bx, &By, &Ex, &Ey, &StemR);
    if (InTriangle(Ox, Oy, TipX, TipY, Lx, Ly, Rx, Ry)) {
        return 1;
    }
    /* 尾巴只接在底边中点之外，勿画穿三角（穿心会又粗又丑） */
    if (NearSeg(Ox, Oy, Bx, By, Ex, Ey, StemR)) {
        return 1;
    }
    return 0;
}

static void DrawCursorArrow(UINT32 X, UINT32 Y) {
    int TipX;
    int TipY;
    int Lx;
    int Ly;
    int Rx;
    int Ry;
    int Bx;
    int By;
    int Ex;
    int Ey;
    int StemR;
    int MinX;
    int MaxX;
    int MinY;
    int MaxY;
    int Ox;
    int Oy;

    ArrowGeom(&TipX, &TipY, &Lx, &Ly, &Rx, &Ry, &Bx, &By, &Ex, &Ey, &StemR);
    MinX = TipX;
    MaxX = TipX;
    MinY = TipY;
    MaxY = TipY;
    if (Lx < MinX) {
        MinX = Lx;
    }
    if (Rx < MinX) {
        MinX = Rx;
    }
    if (Ex < MinX) {
        MinX = Ex;
    }
    if (Lx > MaxX) {
        MaxX = Lx;
    }
    if (Rx > MaxX) {
        MaxX = Rx;
    }
    if (Ex > MaxX) {
        MaxX = Ex;
    }
    if (Ly < MinY) {
        MinY = Ly;
    }
    if (Ry < MinY) {
        MinY = Ry;
    }
    if (Ey < MinY) {
        MinY = Ey;
    }
    if (Ly > MaxY) {
        MaxY = Ly;
    }
    if (Ry > MaxY) {
        MaxY = Ry;
    }
    if (Ey > MaxY) {
        MaxY = Ey;
    }
    MinX -= StemR + CURSOR_OUTLINE + 1;
    MinY -= StemR + CURSOR_OUTLINE + 1;
    MaxX += StemR + CURSOR_OUTLINE + 1;
    MaxY += StemR + CURSOR_OUTLINE + 1;

    for (Oy = MinY; Oy <= MaxY; Oy++) {
        for (Ox = MinX; Ox <= MaxX; Ox++) {
            if (ArrowSolid(Ox, Oy)) {
                continue;
            }
            if (ArrowSolid(Ox - 1, Oy) || ArrowSolid(Ox + 1, Oy) ||
                ArrowSolid(Ox, Oy - 1) || ArrowSolid(Ox, Oy + 1)) {
                CursorPutPixel((int)X + Ox, (int)Y + Oy, COLOR_BLACK);
            }
        }
    }
    for (Oy = MinY; Oy <= MaxY; Oy++) {
        for (Ox = MinX; Ox <= MaxX; Ox++) {
            if (ArrowSolid(Ox, Oy)) {
                CursorPutPixel((int)X + Ox, (int)Y + Oy, COLOR_WHITE);
            }
        }
    }
}

static void ArrowBBox(int *MinX, int *MaxX, int *MinY, int *MaxY) {
    int TipX;
    int TipY;
    int Lx;
    int Ly;
    int Rx;
    int Ry;
    int Bx;
    int By;
    int Ex;
    int Ey;
    int StemR;

    ArrowGeom(&TipX, &TipY, &Lx, &Ly, &Rx, &Ry, &Bx, &By, &Ex, &Ey, &StemR);
    *MinX = TipX;
    *MaxX = TipX;
    *MinY = TipY;
    *MaxY = TipY;
    if (Lx < *MinX) {
        *MinX = Lx;
    }
    if (Rx < *MinX) {
        *MinX = Rx;
    }
    if (Ex < *MinX) {
        *MinX = Ex;
    }
    if (Lx > *MaxX) {
        *MaxX = Lx;
    }
    if (Rx > *MaxX) {
        *MaxX = Rx;
    }
    if (Ex > *MaxX) {
        *MaxX = Ex;
    }
    if (Ly < *MinY) {
        *MinY = Ly;
    }
    if (Ry < *MinY) {
        *MinY = Ry;
    }
    if (Ey < *MinY) {
        *MinY = Ey;
    }
    if (Ly > *MaxY) {
        *MaxY = Ly;
    }
    if (Ry > *MaxY) {
        *MaxY = Ry;
    }
    if (Ey > *MaxY) {
        *MaxY = Ey;
    }
    *MinX -= StemR + CURSOR_OUTLINE + 1;
    *MinY -= StemR + CURSOR_OUTLINE + 1;
    *MaxX += StemR + CURSOR_OUTLINE + 1;
    *MaxY += StemR + CURSOR_OUTLINE + 1;
}

int CursorGlyphToArgb(int Kind, UINT32 *Out, UINT32 *HotX, UINT32 *HotY) {
    UINT32 i;
    int MinX;
    int MaxX;
    int MinY;
    int MaxY;
    int Axis;
    int Half;
    int Thick;
    int Ext;
    int NeedW;
    int NeedH;
    UINT32 Pw;
    UINT32 Ph;

    if (!Out || !HotX || !HotY) {
        return 0;
    }
    /*
     * 栅格比例必须与绘制变换同源（Num/Den），不能用 UI 档位整数：
     * 150% 的 3/2、以及超大逻辑屏被限幅后的实际比例都能对上。
     */
    gGlyphNum = HalVideoGetDrawNum();
    gGlyphDen = HalVideoGetDrawDen();
    if (gGlyphNum == 0) {
        gGlyphNum = 1;
    }
    if (gGlyphDen == 0) {
        gGlyphDen = 1;
    }
    /*
     * 以物理高为 100% 参考，再按 Num/Den 放大。这样 100/150/200% 的
     * Head/Stem/Wing 同比例，尾巴不会被「Half 下限」吃掉。
     */
    HalVideoGetPhysicalSize(&Pw, &Ph);
    /*
     * 只对 ≥100% 的档位用物理高参考；50% 保持原来的「不缩小光标」行为，
     * 避免动到已验收的 B（50% 布局）。
     */
    gCursorMetricH = (Ph != 0 && gGlyphNum >= gGlyphDen) ? Ph : 0;
    for (i = 0; i < CURSOR_HW_DIM * CURSOR_HW_DIM; i++) {
        Out[i] = 0;
    }

    gGlyphBuf = Out;
    gGlyphOrigX = 0;
    gGlyphOrigY = 0;

    if (Kind == CURSOR_KIND_RESIZE_E) {
        Axis = 0;
    } else if (Kind == CURSOR_KIND_RESIZE_S) {
        Axis = 1;
    } else if (Kind == CURSOR_KIND_RESIZE_SE) {
        Axis = 2;
    } else {
        Axis = -1;
    }

    if (Axis < 0) {
        ArrowBBox(&MinX, &MaxX, &MinY, &MaxY);
        gGlyphHotX = GlyphHotFromLogical(MinX);
        gGlyphHotY = GlyphHotFromLogical(MinY);
        NeedW = gGlyphHotX + GlyphEndFromLogical(MaxX);
        NeedH = gGlyphHotY + GlyphEndFromLogical(MaxY);
        if (NeedW > CURSOR_HW_DIM || NeedH > CURSOR_HW_DIM) {
            /* 位图装不下：退回 1:1（仍是完整箭头，只是不放大） */
            gGlyphNum = 1;
            gGlyphDen = 1;
            gGlyphHotX = GlyphHotFromLogical(MinX);
            gGlyphHotY = GlyphHotFromLogical(MinY);
            NeedW = gGlyphHotX + GlyphEndFromLogical(MaxX);
            NeedH = gGlyphHotY + GlyphEndFromLogical(MaxY);
        }
        if (NeedW > CURSOR_HW_DIM || NeedH > CURSOR_HW_DIM) {
            gGlyphBuf = 0;
            gCursorMetricH = 0;
            return 0;
        }
        DrawCursorArrow(0, 0);
    } else {
        CursorMetrics(&Half, &Thick);
        Ext = Half + Half / 2 + Thick + CURSOR_OUTLINE + 2;
        gGlyphHotX = GlyphHotFromLogical(-Ext);
        gGlyphHotY = gGlyphHotX;
        NeedW = gGlyphHotX + GlyphEndFromLogical(Ext);
        NeedH = NeedW;
        if (NeedW > CURSOR_HW_DIM || NeedH > CURSOR_HW_DIM) {
            gGlyphNum = 1;
            gGlyphDen = 1;
            if (Ext > (int)(CURSOR_HW_DIM / 2u) - 1) {
                Ext = (int)(CURSOR_HW_DIM / 2u) - 1;
            }
            gGlyphHotX = Ext;
            gGlyphHotY = Ext;
        }
        DrawCursorAxis(0, 0, Axis);
    }

    gGlyphBuf = 0;
    gCursorMetricH = 0;
    *HotX = (UINT32)gGlyphHotX;
    *HotY = (UINT32)gGlyphHotY;
    return 1;
}

void DrawCursorGlyph(UINT32 X, UINT32 Y) {
    if (gGuiWindowManager.CursorKind == CURSOR_KIND_RESIZE_E) {
        DrawCursorAxis(X, Y, 0);
    } else if (gGuiWindowManager.CursorKind == CURSOR_KIND_RESIZE_S) {
        DrawCursorAxis(X, Y, 1);
    } else if (gGuiWindowManager.CursorKind == CURSOR_KIND_RESIZE_SE) {
        DrawCursorAxis(X, Y, 2);
    } else {
        DrawCursorArrow(X, Y);
    }
}
