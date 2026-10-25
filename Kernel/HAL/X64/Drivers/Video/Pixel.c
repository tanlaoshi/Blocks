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

    if (X >= gScreen.Width || Y >= gScreen.Height) {
        return;
    }
    Fb = DrawBase();
    Pitch = DrawPitch();
    if (!Fb || Pitch == 0) {
        return;
    }
    Fb[Y * Pitch + X] = FbPack(Color);
    DirtyUnion(X, Y, 1, 1);
}

/* 光标 XOR：再异或一次即擦除，无需 save-under/ReadPixel */
void VideoXorPixelRaw(UINT32 X, UINT32 Y, UINT32 Mask) {
    UINT32 *Fb;
    UINT32 Pitch;

    if (X >= gScreen.Width || Y >= gScreen.Height) {
        return;
    }
    Fb = DrawBase();
    Pitch = DrawPitch();
    if (!Fb || Pitch == 0) {
        return;
    }
    Fb[Y * Pitch + X] ^= FbPack(Mask);
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
    UINT32 *Pix;

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
    Pix = &Fb[Y * Pitch + X];
    *Pix = FbPack(VideoBlendRgb(FbPack(*Pix), Color, Alpha));
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

    if (X >= gScreen.Width || Y >= gScreen.Height) {
        return 0;
    }
    Fb = DrawBase();
    Pitch = DrawPitch();
    if (!Fb || Pitch == 0) {
        return 0;
    }
    return FbPack(Fb[Y * Pitch + X]);
}
