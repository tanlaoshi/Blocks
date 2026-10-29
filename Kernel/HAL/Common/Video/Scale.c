/*
 * Scale.c — UI 缩放 / 逻辑分辨率 / VideoSet（PR-H-video-split-1）
 *
 * 从 Video.c 迁出；只搬家、不改逻辑。
 * VideoSet 与 ApplyLogicalFromPhys 同文件（初始化即算逻辑分辨率）。
 */
#include "Private.h"

UINT32 NormalizeUiScale(UINT32 Percent) {
    if (Percent <= 75) {
        return 50;
    }
    if (Percent <= 125) {
        return 100;
    }
    if (Percent <= 175) {
        return 150;
    }
    return 200;
}

/*
 * 绘制比例 = gUiScale/100 约到最简分数：50→1/2、100→1/1、150→3/2、200→2/1。
 * 用整数 gcd 化简，不引入浮点（内核禁 FPU 于纯绘制路径）。
 */
static void DrawRatioFromScale(UINT32 Scale, UINT32 *Num, UINT32 *Den) {
    UINT32 A = Scale ? Scale : 100u;
    UINT32 B = 100u;
    UINT32 T;

    while (B != 0u) {
        T = A % B;
        A = B;
        B = T;
    }
    if (A == 0u) {
        *Num = 1u;
        *Den = 1u;
        return;
    }
    *Num = (Scale ? Scale : 100u) / A;
    *Den = 100u / A;
}

/*
 * 逻辑画布像素上限（只约束布局单位，后缓冲恒为物理尺寸）。
 *
 * 32Mi 覆盖到 4K@50%（7680×4320 = 33.18M < 32Mi）。这个上限保护的是按
 * 逻辑尺寸分配的缓存（桌面壁纸 gWallScreen、窗口备份），不是后缓冲。
 * 旧值 8Mi 是「后缓冲 = 逻辑分辨率」时代的遗留：1920×1200@50% 需要
 * 3840×2400（9.2M），被限幅成 2880×1800（5.2M）却仍按 1/2 落笔 →
 * 桌面/任务栏只铺满 3/4，且指针与绘制用不同的逻辑↔物理比例。
 */
#define VIDEO_LOGICAL_PIXEL_CAP (32ull * 1024ull * 1024ull)

static UINT32 ScaleGcd(UINT32 A, UINT32 B) {
    UINT32 T;

    while (B != 0u) {
        T = A % B;
        A = B;
        B = T;
    }
    return A ? A : 1u;
}

/*
 * 逻辑分辨率 = 物理 * Den / Num（Num/Den 与绘制变换同一份有理数）。
 *
 * 若超过像素上限，不能只把逻辑尺寸乘 3/4 而把 Num/Den 留在原档：
 * 那样逻辑 Lw 与物理 gPhysW 不再满足 Lw = PhysW*Den/Num，绘制只覆盖
 * 屏幕的一部分（1920×1200@50% 恰好只剩 3/4），硬件光标 LogToPhys 也会
 * 用另一个比例。限幅后必须把 Num/Den 重算成 PhysW/Lw，保证铺满。
 */
void ApplyLogicalFromPhys(void) {
    UINT32 Lw;
    UINT32 Lh;
    UINT32 Num;
    UINT32 Den;
    UINT32 G;
    UINT32 Scale = gUiScale ? gUiScale : 100u;
    UINT64 Area;

    if (gPhysW == 0 || gPhysH == 0) {
        return;
    }
    DrawRatioFromScale(Scale, &Num, &Den);
    Lw = (UINT32)(((UINT64)gPhysW * Den) / Num);
    Lh = (UINT32)(((UINT64)gPhysH * Den) / Num);
    if (Lw < 320) {
        Lw = 320;
    }
    if (Lh < 240) {
        Lh = 240;
    }
    Area = (UINT64)Lw * (UINT64)Lh;
    if (Area > VIDEO_LOGICAL_PIXEL_CAP) {
        while (Area > VIDEO_LOGICAL_PIXEL_CAP) {
            Lw = (Lw * 3u) / 4u;
            Lh = (Lh * 3u) / 4u;
            if (Lw < 320 || Lh < 240) {
                break;
            }
            Area = (UINT64)Lw * (UINT64)Lh;
        }
        if (Lw < 320 || Lh < 240) {
            /* 极端小屏：退回 100%，逻辑=物理，保证仍能画满 */
            gUiScale = 100;
            Num = 1;
            Den = 1;
            Lw = gPhysW;
            Lh = gPhysH;
        } else {
            /*
             * 用缩小后的 Lw 反推绘制比例，让 Lw 恰好映射到 gPhysW；
             * 再按同一比例重算 Lh，避免宽高比例在取整后漂移。
             */
            G = ScaleGcd(gPhysW, Lw);
            Num = gPhysW / G;
            Den = Lw / G;
            Lh = (UINT32)(((UINT64)gPhysH * Den) / Num);
            if (Lh < 240) {
                Lh = 240;
            }
        }
    }
    gScreen.Width = Lw;
    gScreen.Height = Lh;
    /*
     * PR-23E：绘制变换是「逻辑 → 物理」的有理数比例，后缓冲永远拿物理
     * 分辨率（gDrawW/gDrawH = 物理宽高），所以 Present 恒 1:1。
     * 50%/150% 不再是「小缓冲 + 最近邻放大」，而是当场把逻辑坐标换算到
     * 物理像素（每逻辑像素写 PixelSpan 宽/高的实心块），字形也按物理尺寸
     * 栅格，物理分辨率才真正用上。
     */
    gDrawNum = Num;
    gDrawDen = Den;
    gDrawW = gPhysW;
    gDrawH = gPhysH;
}

void VideoSet(VIDEO_CONFIG *VideoConfig) {
    gPhysW = VideoConfig->HorizontalResolution;
    gPhysH = VideoConfig->VerticalResolution;
    gScreen.PixelsPerScanLine = VideoConfig->PixelsPerScanLine;
    gScreen.FrameBufferBase = VideoConfig->FrameBufferBase;
    gScreen.FrameBufferSize = VideoConfig->FrameBufferSize;
    gScreen.CursorX = 0;
    gScreen.CursorY = 0;
    gFront = (UINT32 *)(UINTN)VideoConfig->FrameBufferBase;
    gFrontPitch = VideoConfig->PixelsPerScanLine;
    /* Pitch*H 可能大于 Width*H；Present/映射须按 Pitch 算 */
    if (gFrontPitch != 0 && VideoConfig->VerticalResolution != 0) {
        UINT64 Layout = (UINT64)gFrontPitch *
                        (UINT64)VideoConfig->VerticalResolution * 4ull;
        if (gScreen.FrameBufferSize < Layout) {
            gScreen.FrameBufferSize = Layout;
        }
    }
    gBack = 0;
    gBackPitch = 0;
    gBackPages = 0;
    gBackOn = 0;
    gDirty = 0;
    gCurDirty = 0;
    gUiScale = NormalizeUiScale(gUiScale);
    ApplyLogicalFromPhys();
}

UINT32 VideoGetUiScale(void) {
    return gUiScale ? gUiScale : 100u;
}

void VideoGetPhysicalSize(UINT32 *Width, UINT32 *Height) {
    if (Width) {
        *Width = gPhysW;
    }
    if (Height) {
        *Height = gPhysH;
    }
}

/*
 * 改 UI 缩放并重算逻辑分辨率；释放后缓冲（调用方再 InitBackbuffer）。
 * Percent 归到 50/100/150/200。
 */
int VideoSetUiScale(UINT32 Percent) {
    UINT32 Next = NormalizeUiScale(Percent);

    if (gPhysW == 0 || gPhysH == 0) {
        gUiScale = Next;
        return -1;
    }
    VideoReleaseBackbuffer();
    gUiScale = Next;
    ApplyLogicalFromPhys();
    gDirty = 0;
    gCurDirty = 0;
    return 0;
}

