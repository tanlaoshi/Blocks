/*
 * Private.h — Video 绘制内部共享头（仅 HAL/Common/Video/）
 *
 * 禁止 Common / User 包含；对外用 Video.h / HalVideo.h。
 */
#ifndef VIDEO_PRIVATE_H
#define VIDEO_PRIVATE_H

#include "Video.h"
#include "Font.h"
#include "PhysicalMemory.h"
#include "Hal.h"

#define PRESENT_CHUNK_ROWS 64u

/* 0=默认 PRESENT_CHUNK_ROWS；拖窗可升到极大值整块 blit，减左右条带频闪 */
extern UINT32 gPresentChunkRows;

/* 全局定义在 Video.c */
extern SCREEN_INFO gScreen;
extern UINT32 gBackground;
extern int gClipOn;
extern UINT32 gClipX;
extern UINT32 gClipY;
extern UINT32 gClipW;
extern UINT32 gClipH;
extern UINT32 gClipBg;

extern UINT32 *gFront;
extern UINT32  gFrontPitch;
extern UINT32 *gBack;
extern UINT32  gBackPitch;
extern UINT32  gBackPages;
extern int     gBackOn;
extern int     gForceFront;

extern UINT32  gPhysW;
extern UINT32  gPhysH;
extern UINT32  gUiScale;

/*
 * PR-23E：绘制变换是有理数比例 Num/Den = gUiScale/100：
 *   50% → 1/2、100% → 1/1、150% → 3/2、200% → 2/1。
 * 后缓冲恒为物理尺寸（gDrawW×gDrawH = 物理宽高），Present 因此永远 1:1；
 * 逻辑坐标在绘制原语里当场换算成物理像素，不再靠 Present 最近邻放大上屏。
 *
 * 为什么必须带 Den：PR-23a 只有整数倍数，150% 只能退化成 S=1 再交给
 * Present 放大 1.5×——字形先按 24 逻辑像素栅格、上屏时糊成重复行列，
 * 物理分辨率白丢。有理数比例让 150% 也能直接按物理像素画 36px 字。
 */
extern UINT32  gDrawNum;
extern UINT32  gDrawDen;
extern UINT32  gDrawW;
extern UINT32  gDrawH;

extern int     gDirty;
extern UINT32  gDx0;
extern UINT32  gDy0;
extern UINT32  gDx1;
extern UINT32  gDy1;
extern int     gCurDirty;
extern UINT32  gCx0;
extern UINT32  gCy0;
extern UINT32  gCx1;
extern UINT32  gCy1;

extern int     gCursorOverlay;
/* Pi VC 等：scanout 为 RGB 字节序时，把 0x00RRGGBB 的 R/B 对调再写 FB */
extern int     gFbSwapRb;

static inline UINT32 FbPack(UINT32 C) {
    if (!gFbSwapRb) {
        return C;
    }
    return (C & 0xFF00FF00u) | ((C & 0xFFu) << 16) | ((C >> 16) & 0xFFu);
}

/* 未初始化时按 1/1 处理，保证 100% 恒等路径 */
static inline UINT32 DrawNum(void) {
    return gDrawNum ? gDrawNum : 1u;
}

static inline UINT32 DrawDen(void) {
    return gDrawDen ? gDrawDen : 1u;
}

/* 后缓冲尺寸；未初始化时退回逻辑尺寸（=物理尺寸的旧行为） */
static inline UINT32 DrawWidth(void) {
    return gDrawW ? gDrawW : gScreen.Width;
}

static inline UINT32 DrawHeight(void) {
    return gDrawH ? gDrawH : gScreen.Height;
}

/* 逻辑坐标 → 物理坐标。坐标一律 floor：ceil 会让右/下边缘多画一像素。 */
static inline UINT32 ScaleX(UINT32 V) {
    return (UINT32)(((UINT64)V * (UINT64)DrawNum()) / (UINT64)DrawDen());
}

static inline UINT32 ScaleY(UINT32 V) {
    return ScaleX(V);
}

/* 逻辑长度 → 物理长度：向上取整，且 V>0 时至少 1 像素（50% 下防塌成 0） */
static inline UINT32 ScaleW(UINT32 V) {
    UINT64 Den;

    if (V == 0u) {
        return 0u;
    }
    Den = (UINT64)DrawDen();
    {
        UINT64 P = ((UINT64)V * (UINT64)DrawNum() + Den - 1u) / Den;

        return (P == 0u) ? 1u : (UINT32)P;
    }
}

static inline UINT32 ScaleH(UINT32 V) {
    return ScaleW(V);
}

/*
 * 物理坐标 → 所属逻辑像素（ScaleX 的逆）：逻辑像素 L 覆盖
 * [ScaleX(L), ScaleX(L+1))，落在里面的物理点必满足 floor(p*Den/Num) == L。
 * 物理直写（PR-23b/c）要用它算裁剪与脏区。
 */
static inline UINT32 UnscaleX(UINT32 V) {
    return (UINT32)(((UINT64)V * (UINT64)DrawDen()) / (UINT64)DrawNum());
}

static inline UINT32 UnscaleY(UINT32 V) {
    return UnscaleX(V);
}

/*
 * 矩形右/下端点的物理坐标。逻辑宽度 853（150% 下 1280*100/150）时
 * ScaleX(853) = 1279，最后一条物理列 1279 就没有逻辑像素覆盖、永远画不到
 * 了（后缓冲未初始化 → 屏幕右/下边缘一条黑边）。所以「够到逻辑屏边」的
 * 端点一律贴到物理宽高：逻辑 853 列 = 物理 0..1279 全部。
 */
static inline UINT32 ScaleEndX(UINT32 V) {
    if (V >= gScreen.Width) {
        return DrawWidth();
    }
    return ScaleX(V);
}

static inline UINT32 ScaleEndY(UINT32 V) {
    if (V >= gScreen.Height) {
        return DrawHeight();
    }
    return ScaleY(V);
}

/* 单个逻辑像素的物理跨度（端点相减；50% 下会塌成 0，故至少 1） */
static inline UINT32 PixelSpanX(UINT32 X) {
    UINT32 A = ScaleX(X);
    UINT32 B = ScaleEndX(X + 1u);

    return (B > A) ? (B - A) : 1u;
}

static inline UINT32 PixelSpanY(UINT32 Y) {
    return PixelSpanX(Y);
}

/*
 * 逻辑矩形 → 物理矩形：Xp = ScaleX(X)、Wp = ScaleX(X+W) - Xp（Y/H 同理）。
 *
 * 为什么端点相减而不是 Wp = ScaleW(W)：相邻矩形 [X,W) 与 [X+W,W2) 若各自
 * 把宽度向上取整，两个取整误差在接缝处方向相反，150% 下就会留下 1 像素
 * 缝（或者重叠多画 1 像素）。端点相减让左矩形右边界与右矩形左边界取到
 * 同一个 ScaleX 值，铺砖接缝必然严丝合缝。
 */
static inline void ScaleRect(UINT32 X, UINT32 Y, UINT32 W, UINT32 H,
                             UINT32 *Xp, UINT32 *Yp, UINT32 *Wp, UINT32 *Hp) {
    UINT32 Ax = ScaleX(X);
    UINT32 Ay = ScaleY(Y);

    if (Xp) {
        *Xp = Ax;
    }
    if (Yp) {
        *Yp = Ay;
    }
    if (Wp) {
        *Wp = ScaleEndX(X + W) - Ax;
    }
    if (Hp) {
        *Hp = ScaleEndY(Y + H) - Ay;
    }
}

static inline UINT32 *DrawBase(void) {
    if (gForceFront && gFront) {
        return gFront;
    }
    return gBackOn ? gBack : gFront;
}

static inline UINT32 DrawPitch(void) {
    if (gForceFront && gFront) {
        return gFrontPitch;
    }
    return gBackOn ? gBackPitch : gFrontPitch;
}

static inline UINT32 CodepointAdvance(UINT32 Cp) {
    return FontCodepointAdvance(Cp);
}

/* Scale / 逻辑分辨率（Scale.c） */
UINT32 NormalizeUiScale(UINT32 Percent);
void ApplyLogicalFromPhys(void);

/* Dirty / Present（Present*.c；Draw* 经 DirtyUnion 记账） */
void DirtyUnionInto(int *Dirty, UINT32 *Dx0, UINT32 *Dy0, UINT32 *Dx1,
                    UINT32 *Dy1, UINT32 X, UINT32 Y, UINT32 W, UINT32 H);
void DirtyUnion(UINT32 X, UINT32 Y, UINT32 W, UINT32 H);
void DirtyUnionCursor(UINT32 X, UINT32 Y, UINT32 W, UINT32 H);
void PresentRectRows(UINT32 X0, UINT32 Y0, UINT32 X1, UINT32 Y1,
                     UINT64 FbBytes, int *DirtyOut, UINT32 *Dx0,
                     UINT32 *Dy0, UINT32 *Dx1, UINT32 *Dy1,
                     int *OutPartial);

#endif
