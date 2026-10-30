/*
 * Place.c — 开窗尺寸预算与不重叠摆放（PR-layout）
 *
 * 为什么先算尺寸：UI 缩放把逻辑分辨率缩小（1280×800 → 853×533 → 640×400），
 * 两个 800×560 的窗在 150%/200% 下就算坐标是逻辑单位也放不下；只挪位置
 * 永远会叠。预算按「同屏两窗」留位：宽 ≤（可用宽 - 2*Margin - 间距）/2，
 * 高 ≤ 可用高 - 2*Margin。100% 大屏（如 1920×1080）预算不触发，尺寸不变。
 *
 * 位置：先试应用首选点；有重叠就按「物理步长」级联（150% 逻辑步长 22、
 * 200% 16，落到屏上都是 ≥32 物理像素）；级联越界回绕到左上；仍找不到就
 * 3/4 缩窗重试；最后选物理重叠面积最小的位置（正常两窗不会走到这一步）。
 */
#include "GuiPrivate.h"
#include "Desktop.h"
#include "HalVideo.h"

static UINT32 PlaceNum(void) {
    UINT32 Num = HalVideoGetDrawNum();

    return Num ? Num : 1u;
}

static UINT32 PlaceDen(void) {
    UINT32 Den = HalVideoGetDrawDen();

    return Den ? Den : 1u;
}

/* 物理长度 → 逻辑长度向上取整：150% 下 32px 步长 = 22 逻辑，落到屏上 ≥32 */
static UINT32 PlaceLogicalSpan(UINT32 Phys) {
    UINT64 Num = (UINT64)PlaceNum();
    UINT64 Den = (UINT64)PlaceDen();

    return (UINT32)(((UINT64)Phys * Den + Num - 1u) / Num);
}

static void PlaceUsable(UINT32 *Width, UINT32 *Height) {
    UINT32 Bar = DesktopTaskbarHeight();

    *Width = gGuiWindowManager.ScreenWidth;
    *Height = gGuiWindowManager.ScreenHeight;
    *Height = (*Height > Bar) ? (*Height - Bar) : 0u;
}

static void PlaceRect(UINT32 X, UINT32 Y, UINT32 W, UINT32 H, UINT32 *OutX,
                      UINT32 *OutY, UINT32 *OutW, UINT32 *OutH) {
    GuiPhysRect(X, Y, W, H, OutX, OutY, OutW, OutH);
}

static int PlaceFits(int Index, UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    UINT32 Ax;
    UINT32 Ay;
    UINT32 Aw;
    UINT32 Ah;
    int i;

    PlaceRect(X, Y, W, H, &Ax, &Ay, &Aw, &Ah);
    for (i = 0; i < MAX_WINS; i++) {
        const GUI_WINDOW *Other = &gGuiWindowManager.Windows[i];
        UINT32 Bx;
        UINT32 By;
        UINT32 Bw;
        UINT32 Bh;

        if (i == Index || !Other->Active) {
            continue;
        }
        PlaceRect(Other->X, Other->Y, Other->Width, Other->Height, &Bx, &By,
                  &Bw, &Bh);
        if (RectIntersects(Ax, Ay, Aw, Ah, Bx, By, Bw, Bh)) {
            return 0;
        }
    }
    return 1;
}

static int PlaceTry(int Index, UINT32 X, UINT32 Y, UINT32 W, UINT32 H,
                    UINT32 *OutX, UINT32 *OutY) {
    UINT32 Uw;
    UINT32 Uh;

    PlaceUsable(&Uw, &Uh);
    if (W > Uw || H > Uh) {
        return 0;
    }
    if (X > Uw - W) {
        X = Uw - W;
    }
    if (Y > Uh - H) {
        Y = Uh - H;
    }
    if (!PlaceFits(Index, X, Y, W, H)) {
        return 0;
    }
    *OutX = X;
    *OutY = Y;
    return 1;
}

/* 首选点 → 对角级联 → 回绕到左上 → 横向/纵向级联 */
static int PlaceCascade(int Index, UINT32 PrefX, UINT32 PrefY, UINT32 W,
                        UINT32 H, UINT32 *OutX, UINT32 *OutY) {
    UINT32 Step = PlaceLogicalSpan(GUI_PLACE_STEP_PHYS);
    int k;

    if (PlaceTry(Index, PrefX, PrefY, W, H, OutX, OutY)) {
        return 1;
    }
    for (k = 1; k <= MAX_WINS * 2; k++) {
        UINT32 D = (UINT32)k * Step;

        if (PlaceTry(Index, PrefX + D, PrefY + D, W, H, OutX, OutY)) {
            return 1;
        }
        if (PlaceTry(Index, GUI_PLACE_EDGE + D, GUI_PLACE_EDGE + D, W, H,
                     OutX, OutY)) {
            return 1;
        }
        if (PlaceTry(Index, PrefX + D, PrefY, W, H, OutX, OutY)) {
            return 1;
        }
        if (PlaceTry(Index, PrefX, PrefY + D, W, H, OutX, OutY)) {
            return 1;
        }
    }
    return 0;
}

static UINT32 PlaceOverlapArea(int Index, UINT32 X, UINT32 Y, UINT32 W,
                               UINT32 H) {
    UINT32 Ax;
    UINT32 Ay;
    UINT32 Aw;
    UINT32 Ah;
    UINT32 Area = 0;
    int i;

    PlaceRect(X, Y, W, H, &Ax, &Ay, &Aw, &Ah);
    for (i = 0; i < MAX_WINS; i++) {
        const GUI_WINDOW *Other = &gGuiWindowManager.Windows[i];
        UINT32 Bx;
        UINT32 By;
        UINT32 Bw;
        UINT32 Bh;
        UINT32 L;
        UINT32 T;
        UINT32 R;
        UINT32 B;

        if (i == Index || !Other->Active) {
            continue;
        }
        PlaceRect(Other->X, Other->Y, Other->Width, Other->Height, &Bx, &By,
                  &Bw, &Bh);
        L = (Ax > Bx) ? Ax : Bx;
        T = (Ay > By) ? Ay : By;
        R = (Ax + Aw < Bx + Bw) ? (Ax + Aw) : (Bx + Bw);
        B = (Ay + Ah < By + Bh) ? (Ay + Ah) : (By + Bh);
        if (L < R && T < B) {
            Area += (R - L) * (B - T);
        }
    }
    return Area;
}

/* 网格扫描：返回 1=找到零重叠；否则把最小重叠位置写回，返回 0 */
static int PlaceScan(int Index, UINT32 W, UINT32 H, UINT32 *OutX,
                     UINT32 *OutY) {
    UINT32 Uw;
    UINT32 Uh;
    UINT32 X;
    UINT32 Y;
    UINT32 Best = 0xFFFFFFFFu;
    UINT32 BestX = 0;
    UINT32 BestY = 0;

    PlaceUsable(&Uw, &Uh);
    if (W > Uw || H > Uh) {
        return 0;
    }
    for (Y = 0; Y + H <= Uh; Y += GUI_PLACE_SCAN_STEP) {
        for (X = 0; X + W <= Uw; X += GUI_PLACE_SCAN_STEP) {
            UINT32 Area = PlaceOverlapArea(Index, X, Y, W, H);

            if (Area < Best) {
                Best = Area;
                BestX = X;
                BestY = Y;
                if (Area == 0u) {
                    *OutX = X;
                    *OutY = Y;
                    return 1;
                }
            }
        }
    }
    *OutX = BestX;
    *OutY = BestY;
    return (Best == 0u) ? 1 : 0;
}

static UINT32 PlaceShrink(UINT32 Value, UINT32 Min) {
    UINT32 Next = (Value * GUI_PLACE_SHRINK_PERCENT) / 100u;

    if (Next < Min) {
        Next = Min;
    }
    return Next;
}

void GuiPlaceWindow(int Index, UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H) {
    UINT32 PrefX;
    UINT32 PrefY;
    UINT32 OrigW;
    UINT32 OrigH;
    UINT32 OutX;
    UINT32 OutY;
    UINT32 Uw;
    UINT32 Uh;
    UINT32 Try;

    if (!X || !Y || !W || !H || *W == 0u || *H == 0u) {
        return;
    }
    PlaceUsable(&Uw, &Uh);
    if (*W > Uw) {
        *W = Uw;
    }
    if (*H > Uh) {
        *H = Uh;
    }
    PrefX = *X;
    PrefY = *Y;
    OrigW = *W;
    OrigH = *H;
    OutX = PrefX;
    OutY = PrefY;
    if (PlaceCascade(Index, PrefX, PrefY, *W, *H, &OutX, &OutY)) {
        *X = OutX;
        *Y = OutY;
        return;
    }
    for (Try = 0; Try < GUI_PLACE_SHRINK_TRIES; Try++) {
        if (*W <= GUI_PLACE_MIN_WIDTH && *H <= GUI_PLACE_MIN_HEIGHT) {
            break;
        }
        if (*W > GUI_PLACE_MIN_WIDTH) {
            *W = PlaceShrink(*W, GUI_PLACE_MIN_WIDTH);
        }
        if (*H > GUI_PLACE_MIN_HEIGHT) {
            *H = PlaceShrink(*H, GUI_PLACE_MIN_HEIGHT);
        }
        if (PlaceCascade(Index, PrefX, PrefY, *W, *H, &OutX, &OutY)) {
            *X = OutX;
            *Y = OutY;
            return;
        }
    }
    /*
     * 缩到最小也塞不下（第 3 个窗 / 用户手动拉大过）：恢复原尺寸，
     * 选物理重叠面积最小的位置。宁可叠一点，也不要把可读的窗缩成一条。
     */
    *W = OrigW;
    *H = OrigH;
    (void)PlaceScan(Index, *W, *H, &OutX, &OutY);
    *X = OutX;
    *Y = OutY;
}

void GuiPlaceSizeBudget(UINT32 Margin, UINT32 *W, UINT32 *H) {
    UINT32 Uw;
    UINT32 Uh;
    UINT32 Gap;
    UINT32 MaxW;
    UINT32 MaxH;
    UINT32 Side;

    if (!W || !H) {
        return;
    }
    PlaceUsable(&Uw, &Uh);
    Gap = PlaceLogicalSpan(GUI_PLACE_GAP_PHYS);
    Side = Margin * 2u + Gap;
    MaxW = (Uw > Side) ? (Uw - Side) / 2u : Uw;
    MaxH = (Uh > Margin * 2u) ? (Uh - Margin * 2u) : Uh;
    if (*W > MaxW) {
        *W = MaxW;
    }
    if (*H > MaxH) {
        *H = MaxH;
    }
    if (*W > Uw) {
        *W = Uw;
    }
    if (*H > Uh) {
        *H = Uh;
    }
}
