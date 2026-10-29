/*
 * Pixel.c — 像素读写与混合（gui-g7-cursor 分脏旁注）
 *
 * DirtyUnion（内容 gDirty）← DrawPixelRaw / Blend / 普通画
 * DirtyUnionCursor（光标 gCurDirty）← XorPixelRaw；以及 CursorOverlay 期内
 *   DirtyUnion 被重定向（Present.c：gCursorOverlay）— WriteRect/画字形勿并进
 *   内容 AABB，否则 Shell∪远处光标 → 近全屏 Present
 *
 * 光标主路径用 save-under（Cursor.c Read/WriteRect），非 XOR；
 * XOR 保留给旧/特殊路径，仍必须走光标脏。编排见 Present.c（G6）。
 */
#include "Private.h"

void VideoDrawPixelRaw(UINT32 X, UINT32 Y, UINT32 Color) {
    UINT32 *Fb;
    UINT32 Pitch;
    UINT32 S;
    UINT32 Sx;
    UINT32 Sy;

    if (X >= gScreen.Width || Y >= gScreen.Height) {
        return;
    }
    Fb = DrawBase();
    Pitch = DrawPitch();
    if (!Fb || Pitch == 0) {
        return;
    }
    /* PR-23a：逻辑像素写成 S×S 实心块；S=1 时与原路径逐字等价 */
    S = DrawScale();
    Color = FbPack(Color);
    for (Sy = 0; Sy < S; Sy++) {
        UINT32 *Line = &Fb[(Y * S + Sy) * Pitch + X * S];

        for (Sx = 0; Sx < S; Sx++) {
            Line[Sx] = Color;
        }
    }
    DirtyUnion(X, Y, 1, 1);
}

/* 光标 XOR：再异或一次即擦除，无需 save-under/ReadPixel */
void VideoXorPixelRaw(UINT32 X, UINT32 Y, UINT32 Mask) {
    UINT32 *Fb;
    UINT32 Pitch;
    UINT32 S;
    UINT32 Sx;
    UINT32 Sy;

    if (X >= gScreen.Width || Y >= gScreen.Height) {
        return;
    }
    Fb = DrawBase();
    Pitch = DrawPitch();
    if (!Fb || Pitch == 0) {
        return;
    }
    S = DrawScale();
    Mask = FbPack(Mask);
    for (Sy = 0; Sy < S; Sy++) {
        UINT32 *Line = &Fb[(Y * S + Sy) * Pitch + X * S];

        for (Sx = 0; Sx < S; Sx++) {
            Line[Sx] ^= Mask;
        }
    }
    /* 勿 DirtyUnion 进内容脏区：4K 下 Shell∪远处光标会并成近全屏 Present */
    DirtyUnionCursor(X, Y, 1, 1);
}

void VideoDrawPixel(UINT32 X, UINT32 Y, UINT32 Color) {
    if (gClipOn) {
        if (X < gClipX || Y < gClipY ||
            X >= gClipX + gClipW || Y >= gClipY + gClipH) {
            return;
        }
    }
    VideoDrawPixelRaw(X, Y, Color);
}

/* PR-GUI-alpha：Src 盖在 Dst 上（0x00RRGGBB）；无浮点 */
UINT32 VideoBlendRgb(UINT32 Dst, UINT32 Src, UINT8 Alpha) {
    UINT32 Inv;
    UINT32 R;
    UINT32 G;
    UINT32 B;

    if (Alpha == 0) {
        return Dst;
    }
    if (Alpha == 255) {
        return Src & 0x00FFFFFFu;
    }
    Inv = 255u - (UINT32)Alpha;
    R = (((Src >> 16) & 0xFFu) * (UINT32)Alpha + ((Dst >> 16) & 0xFFu) * Inv) / 255u;
    G = (((Src >> 8) & 0xFFu) * (UINT32)Alpha + ((Dst >> 8) & 0xFFu) * Inv) / 255u;
    B = ((Src & 0xFFu) * (UINT32)Alpha + (Dst & 0xFFu) * Inv) / 255u;
    return (R << 16) | (G << 8) | B;
}

void VideoBlendPixelRaw(UINT32 X, UINT32 Y, UINT32 Color, UINT8 Alpha) {
    UINT32 *Fb;
    UINT32 Pitch;
    UINT32 S;
    UINT32 Sx;
    UINT32 Sy;

    if (Alpha == 0) {
        return;
    }
    if (Alpha == 255) {
        VideoDrawPixelRaw(X, Y, Color);
        return;
    }
    if (X >= gScreen.Width || Y >= gScreen.Height) {
        return;
    }
    Fb = DrawBase();
    Pitch = DrawPitch();
    if (!Fb || Pitch == 0) {
        return;
    }
    /* 每个物理子像素各 blend 一次；块内同色时与先逻辑混合再放大等价 */
    S = DrawScale();
    for (Sy = 0; Sy < S; Sy++) {
        UINT32 *Line = &Fb[(Y * S + Sy) * Pitch + X * S];

        for (Sx = 0; Sx < S; Sx++) {
            Line[Sx] = FbPack(VideoBlendRgb(FbPack(Line[Sx]), Color, Alpha));
        }
    }
    DirtyUnion(X, Y, 1, 1);
}

void VideoBlendPixel(UINT32 X, UINT32 Y, UINT32 Color, UINT8 Alpha) {
    if (gClipOn) {
        if (X < gClipX || Y < gClipY ||
            X >= gClipX + gClipW || Y >= gClipY + gClipH) {
            return;
        }
    }
    VideoBlendPixelRaw(X, Y, Color, Alpha);
}

UINT32 VideoReadPixel(UINT32 X, UINT32 Y) {
    UINT32 *Fb;
    UINT32 Pitch;
    UINT32 S;

    if (X >= gScreen.Width || Y >= gScreen.Height) {
        return 0;
    }
    Fb = DrawBase();
    Pitch = DrawPitch();
    if (!Fb || Pitch == 0) {
        return 0;
    }
    /* 逻辑像素对应该块的左上角；块内同色，save-under 读回可原样还原 */
    S = DrawScale();
    return FbPack(Fb[Y * S * Pitch + X * S]);
}

/*
 * PR-23b：物理像素 1:1 读写。
 *
 * 坐标已是物理像素（调用方用 逻辑×S 算好），这里绝不能再乘 DrawScale，
 * 否则 200% 下会写成 2×2 块，物理分辨率栅格就白做了。
 * gClip* / 脏区仍是逻辑坐标：物理像素 (Xp,Yp) 属于逻辑像素
 * (Xp/S,Yp/S)，裁剪与 DirtyUnion 都按它算，Present 再乘回 S。
 */
static int PhysPixelOk(UINT32 Xp, UINT32 Yp, UINT32 *OutLx, UINT32 *OutLy) {
    UINT32 S = DrawScale();
    UINT32 Lx = Xp / S;
    UINT32 Ly = Yp / S;
    UINT32 W = gDrawW ? gDrawW : gScreen.Width * S;
    UINT32 H = gDrawH ? gDrawH : gScreen.Height * S;

    if (Xp >= W || Yp >= H) {
        return 0;
    }
    if (gClipOn) {
        if (Lx < gClipX || Ly < gClipY ||
            Lx >= gClipX + gClipW || Ly >= gClipY + gClipH) {
            return 0;
        }
    }
    if (OutLx) {
        *OutLx = Lx;
    }
    if (OutLy) {
        *OutLy = Ly;
    }
    return 1;
}

void VideoDrawPixelPhys(UINT32 Xp, UINT32 Yp, UINT32 Color) {
    UINT32 *Fb;
    UINT32 Pitch;
    UINT32 Lx;
    UINT32 Ly;

    if (!PhysPixelOk(Xp, Yp, &Lx, &Ly)) {
        return;
    }
    Fb = DrawBase();
    Pitch = DrawPitch();
    if (!Fb || Pitch == 0) {
        return;
    }
    Fb[Yp * Pitch + Xp] = FbPack(Color);
    DirtyUnion(Lx, Ly, 1, 1);
}

UINT32 VideoReadPixelPhys(UINT32 Xp, UINT32 Yp) {
    UINT32 *Fb;
    UINT32 Pitch;

    if (!PhysPixelOk(Xp, Yp, 0, 0)) {
        return 0;
    }
    Fb = DrawBase();
    Pitch = DrawPitch();
    if (!Fb || Pitch == 0) {
        return 0;
    }
    return FbPack(Fb[Yp * Pitch + Xp]);
}

void VideoBlendPixelPhys(UINT32 Xp, UINT32 Yp, UINT32 Color, UINT8 Alpha) {
    UINT32 *Fb;
    UINT32 Pitch;
    UINT32 Lx;
    UINT32 Ly;

    if (Alpha == 0) {
        return;
    }
    if (Alpha == 255) {
        VideoDrawPixelPhys(Xp, Yp, Color);
        return;
    }
    if (!PhysPixelOk(Xp, Yp, &Lx, &Ly)) {
        return;
    }
    Fb = DrawBase();
    Pitch = DrawPitch();
    if (!Fb || Pitch == 0) {
        return;
    }
    Fb[Yp * Pitch + Xp] = FbPack(VideoBlendRgb(FbPack(Fb[Yp * Pitch + Xp]),
                                               Color, Alpha));
    DirtyUnion(Lx, Ly, 1, 1);
}
