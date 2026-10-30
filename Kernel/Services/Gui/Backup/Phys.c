/*
 * Phys.c — PR-23d：逻辑坐标 → 物理像素（窗备份 / 拖动 under 换算）
 *
 * 口径与 Cursor.c 的 CursorPhysRect 一致：起点下取整、末端上取整、宽高用
 * 端点相减。100%（Num=Den=1）恒等；150%（3/2）下一个逻辑格覆盖 1~2 个
 * 物理像素，端点差保证整块逻辑区被完整覆盖、且不多读一列。
 *
 * 为什么不能拿 HalVideoGetDrawScale() 乘：它的语义是「一个逻辑像素最多
 * 占几个物理像素」（150% 时 2），当坐标倍数用会偏半格——PR-23c 已被这个
 * 坑咬过一次（光标残影）。
 */
#include "GuiPrivate.h"
#include "HalVideo.h"

static UINT32 PhysNum(void) {
    UINT32 Num = HalVideoGetDrawNum();

    return Num ? Num : 1u;
}

static UINT32 PhysDen(void) {
    UINT32 Den = HalVideoGetDrawDen();

    return Den ? Den : 1u;
}

UINT32 GuiPhysX(UINT32 X) {
    return (UINT32)(((UINT64)X * (UINT64)PhysNum()) / (UINT64)PhysDen());
}

UINT32 GuiPhysY(UINT32 Y) {
    return (UINT32)(((UINT64)Y * (UINT64)PhysNum()) / (UINT64)PhysDen());
}

void GuiPhysRect(UINT32 X, UINT32 Y, UINT32 W, UINT32 H,
                 UINT32 *Xp, UINT32 *Yp, UINT32 *Wp, UINT32 *Hp) {
    UINT64 Num = (UINT64)PhysNum();
    UINT64 Den = (UINT64)PhysDen();
    UINT32 X0 = GuiPhysX(X);
    UINT32 Y0 = GuiPhysY(Y);
    UINT32 X1 = (UINT32)((((UINT64)(X + W)) * Num + Den - 1u) / Den);
    UINT32 Y1 = (UINT32)((((UINT64)(Y + H)) * Num + Den - 1u) / Den);

    if (X1 <= X0) {
        X1 = X0 + 1u;
    }
    if (Y1 <= Y0) {
        Y1 = Y0 + 1u;
    }
    if (Xp) {
        *Xp = X0;
    }
    if (Yp) {
        *Yp = Y0;
    }
    if (Wp) {
        *Wp = X1 - X0;
    }
    if (Hp) {
        *Hp = Y1 - Y0;
    }
}
