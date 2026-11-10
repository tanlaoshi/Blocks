/*
 * CursorGeometry.c — save-under 盒与逻辑→物理矩形（自 Cursor.c 纯搬）
 *
 * PR-23c：Under 按物理像素读写；Save* 仍存逻辑值。
 * PR-23E：用 Num/Den 有理换算，勿 X*DrawScale。
 */
#include "GuiPrivate.h"
#include "HalVideo.h"
#include "UI.h"

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

/*
 * PR-23E：逻辑矩形 → 物理矩形。用有理数 Num/Den（50/100/150/200 →
 * 1/2、1/1、3/2、2/1），起点向下取整、末端向上取整，宽度用端点相减。
 *
 * 不能再用「X * HalVideoGetDrawScale()」：那个值现在的语义是「一个逻辑
 * 像素最多占几个物理像素」（150% 时为 2），拿它当坐标倍数会让 save-under
 * 偏半格 —— 光标画在 1.5X、存却在 2X，移开时把旁边区域盖回来，表现为
 * 150% 下残留一枚光标箭头。
 */
void CursorPhysicalRect(UINT32 X, UINT32 Y, UINT32 W, UINT32 H,
                           UINT32 *Xp, UINT32 *Yp, UINT32 *Wp, UINT32 *Hp) {
    UINT32 Num = HalVideoGetDrawNum();
    UINT32 Den = HalVideoGetDrawDen();
    UINT32 X0;
    UINT32 Y0;
    UINT32 X1;
    UINT32 Y1;

    if (Num == 0) {
        Num = 1;
    }
    if (Den == 0) {
        Den = 1;
    }
    X0 = (UINT32)((UINT64)X * Num / Den);
    Y0 = (UINT32)((UINT64)Y * Num / Den);
    X1 = (UINT32)(((UINT64)(X + W) * Num + Den - 1u) / Den);
    Y1 = (UINT32)(((UINT64)(Y + H) * Num + Den - 1u) / Den);
    if (X1 <= X0) {
        X1 = X0 + 1u;
    }
    if (Y1 <= Y0) {
        Y1 = Y0 + 1u;
    }
    *Xp = X0;
    *Yp = Y0;
    *Wp = X1 - X0;
    *Hp = Y1 - Y0;
}

void DrawCursorAt(UINT32 X, UINT32 Y) {
    DrawCursorGlyph(X, Y);
}
