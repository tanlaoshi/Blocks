/*
 * Fill.c — 矩形填充（PR-S-video-1）
 */
#include "Private.h"

void VideoFillRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color) {
    UINT32 *Fb;
    UINT32 Pitch;
    UINT32 Row;
    UINT32 Col;
    UINT32 CopyW;
    UINT32 CopyH;
    UINT32 X0;
    UINT32 Y0;
    UINT32 Xp;
    UINT32 Yp;
    UINT32 Wp;
    UINT32 Hp;

    if (!Width || !Height) {
        return;
    }
    Fb = DrawBase();
    Pitch = DrawPitch();
    if (!Fb || Pitch == 0) {
        return;
    }
    X0 = X;
    Y0 = Y;
    CopyW = Width;
    CopyH = Height;

    /* PR-G10 M5：与 DrawPixel 一致，尊重客户区 clip */
    if (gClipOn) {
        UINT32 ClipR = gClipX + gClipW;
        UINT32 ClipB = gClipY + gClipH;
        UINT32 R;
        UINT32 B;

        if (X0 >= ClipR || Y0 >= ClipB) {
            return;
        }
        if (X0 < gClipX) {
            UINT32 Skip = gClipX - X0;
            if (Skip >= CopyW) {
                return;
            }
            CopyW -= Skip;
            X0 = gClipX;
        }
        if (Y0 < gClipY) {
            UINT32 Skip = gClipY - Y0;
            if (Skip >= CopyH) {
                return;
            }
            CopyH -= Skip;
            Y0 = gClipY;
        }
        if (CopyW == 0 || CopyH == 0) {
            return;
        }
        R = X0 + CopyW;
        B = Y0 + CopyH;
        if (R > ClipR) {
            CopyW = ClipR - X0;
        }
        if (B > ClipB) {
            CopyH = ClipB - Y0;
        }
    }

    if (X0 >= gScreen.Width || Y0 >= gScreen.Height) {
        return;
    }
    if (X0 + CopyW > gScreen.Width) {
        CopyW = gScreen.Width - X0;
    }
    if (Y0 + CopyH > gScreen.Height) {
        CopyH = gScreen.Height - Y0;
    }
    if (CopyW == 0 || CopyH == 0) {
        return;
    }
    /* 脏矩形仍用逻辑坐标；落笔前按有理数比例换成物理矩形（PR-23E） */
    DirtyUnion(X0, Y0, CopyW, CopyH);
    ScaleRect(X0, Y0, CopyW, CopyH, &Xp, &Yp, &Wp, &Hp);
    if (Xp >= DrawWidth() || Yp >= DrawHeight()) {
        return;
    }
    if (Wp > DrawWidth() - Xp) {
        Wp = DrawWidth() - Xp;
    }
    if (Hp > DrawHeight() - Yp) {
        Hp = DrawHeight() - Yp;
    }
    Color = FbPack(Color);
    for (Row = 0; Row < Hp; Row++) {
        UINT32 *Line = &Fb[(Yp + Row) * Pitch + Xp];

        for (Col = 0; Col < Wp; Col++) {
            Line[Col] = Color;
        }
    }
}

void VideoBlendFillRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height,
                        UINT32 Color, UINT8 Alpha) {
    UINT32 *Fb;
    UINT32 Pitch;
    UINT32 Row;
    UINT32 Col;
    UINT32 CopyW;
    UINT32 CopyH;
    UINT32 X0;
    UINT32 Y0;
    UINT32 Xp;
    UINT32 Yp;
    UINT32 Wp;
    UINT32 Hp;

    if (Alpha == 0 || !Width || !Height) {
        return;
    }
    if (Alpha == 255) {
        VideoFillRect(X, Y, Width, Height, Color);
        return;
    }
    Fb = DrawBase();
    Pitch = DrawPitch();
    if (!Fb || Pitch == 0) {
        return;
    }
    X0 = X;
    Y0 = Y;
    CopyW = Width;
    CopyH = Height;

    if (gClipOn) {
        UINT32 ClipR = gClipX + gClipW;
        UINT32 ClipB = gClipY + gClipH;
        UINT32 R;
        UINT32 B;

        if (X0 >= ClipR || Y0 >= ClipB) {
            return;
        }
        if (X0 < gClipX) {
            UINT32 Skip = gClipX - X0;
            if (Skip >= CopyW) {
                return;
            }
            CopyW -= Skip;
            X0 = gClipX;
        }
        if (Y0 < gClipY) {
            UINT32 Skip = gClipY - Y0;
            if (Skip >= CopyH) {
                return;
            }
            CopyH -= Skip;
            Y0 = gClipY;
        }
        if (CopyW == 0 || CopyH == 0) {
            return;
        }
        R = X0 + CopyW;
        B = Y0 + CopyH;
        if (R > ClipR) {
            CopyW = ClipR - X0;
        }
        if (B > ClipB) {
            CopyH = ClipB - Y0;
        }
    }

    if (X0 >= gScreen.Width || Y0 >= gScreen.Height) {
        return;
    }
    if (X0 + CopyW > gScreen.Width) {
        CopyW = gScreen.Width - X0;
    }
    if (Y0 + CopyH > gScreen.Height) {
        CopyH = gScreen.Height - Y0;
    }
    if (CopyW == 0 || CopyH == 0) {
        return;
    }
    /* 逻辑脏区先记账，再按有理数比例把整块物理矩形逐像素混一遍 */
    DirtyUnion(X0, Y0, CopyW, CopyH);
    ScaleRect(X0, Y0, CopyW, CopyH, &Xp, &Yp, &Wp, &Hp);
    if (Xp >= DrawWidth() || Yp >= DrawHeight()) {
        return;
    }
    if (Wp > DrawWidth() - Xp) {
        Wp = DrawWidth() - Xp;
    }
    if (Hp > DrawHeight() - Yp) {
        Hp = DrawHeight() - Yp;
    }
    for (Row = 0; Row < Hp; Row++) {
        UINT32 *Line = &Fb[(Yp + Row) * Pitch + Xp];

        for (Col = 0; Col < Wp; Col++) {
            Line[Col] = FbPack(VideoBlendRgb(FbPack(Line[Col]), Color, Alpha));
        }
    }
}
