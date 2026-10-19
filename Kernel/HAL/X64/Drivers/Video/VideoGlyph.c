/*
 * VideoGlyph.c — 字符与点阵（PR-S-video-1）
 * PR-GUI-l2-font：1bpp 邻接覆盖做灰度边，不换字形格式。
 */
#include "VideoPrivate.h"

static int gGlyphSmooth;

void VideoSetGlyphSmooth(int On) {
    gGlyphSmooth = On ? 1 : 0;
}

static int GlyphBit(const UINT8 *Glyph, UINT32 Bpr, UINT32 Width, UINT32 Height,
                    INT32 Col, INT32 Row) {
    UINT8 Byte;
    int Bit;

    if (Col < 0 || Row < 0 || (UINT32)Col >= Width || (UINT32)Row >= Height) {
        return 0;
    }
    Byte = Glyph[(UINT32)Row * Bpr + ((UINT32)Col / 8u)];
    Bit = 7 - (int)((UINT32)Col % 8u);
    return (Byte & (1 << Bit)) ? 1 : 0;
}

/* 空像素贴着实心边时给一圈浅灰，台阶不再是硬切 */
static UINT8 EdgeAlpha(const UINT8 *Glyph, UINT32 Bpr, UINT32 Width, UINT32 Height,
                       UINT32 Col, UINT32 Row) {
    int N = 0;

    if (GlyphBit(Glyph, Bpr, Width, Height, (INT32)Col - 1, (INT32)Row)) {
        N++;
    }
    if (GlyphBit(Glyph, Bpr, Width, Height, (INT32)Col + 1, (INT32)Row)) {
        N++;
    }
    if (GlyphBit(Glyph, Bpr, Width, Height, (INT32)Col, (INT32)Row - 1)) {
        N++;
    }
    if (GlyphBit(Glyph, Bpr, Width, Height, (INT32)Col, (INT32)Row + 1)) {
        N++;
    }
    if (N <= 0) {
        return 0;
    }
    if (N == 1) {
        return 112;
    }
    if (N == 2) {
        return 72;
    }
    return 48;
}

/* 覆盖度在线性光下混合。黑字若在 sRGB 里直接乘，边缘会糊成深灰块。 */
static UINT32 IsqrtU(UINT32 N) {
    UINT32 X;
    UINT32 Y;

    if (N == 0) {
        return 0;
    }
    X = N;
    Y = (X + 1u) / 2u;
    while (Y < X) {
        X = Y;
        Y = (X + N / X) / 2u;
    }
    return X;
}

static UINT32 GammaBlend(UINT32 Dst, UINT32 Src, UINT8 Alpha) {
    UINT32 Inv = 255u - (UINT32)Alpha;
    UINT32 Out = 0;
    int Shift;

    for (Shift = 16; Shift >= 0; Shift -= 8) {
        UINT32 S = (Src >> Shift) & 0xFFu;
        UINT32 D = (Dst >> Shift) & 0xFFu;
        UINT32 Sl = (S * S) / 255u;
        UINT32 Dl = (D * D) / 255u;
        UINT32 L = (Sl * (UINT32)Alpha + Dl * Inv) / 255u;
        UINT32 C = IsqrtU(L * 255u);

        if (C > 255u) {
            C = 255u;
        }
        Out |= C << Shift;
    }
    return Out;
}

static void PaintBlock(UINT32 X, UINT32 Y, UINT32 ScaleX, UINT32 ScaleY,
                       UINT32 Color, UINT8 Alpha) {
    UINT32 Sy;
    UINT32 Sx;

    for (Sy = 0; Sy < ScaleY; Sy++) {
        for (Sx = 0; Sx < ScaleX; Sx++) {
            if (Alpha == 255) {
                VideoDrawPixel(X + Sx, Y + Sy, Color);
            } else {
                UINT32 Px = X + Sx;
                UINT32 Py = Y + Sy;
                UINT32 Dst;

                if (Px >= gScreen.Width || Py >= gScreen.Height) {
                    continue;
                }
                Dst = VideoReadPixel(Px, Py);
                VideoDrawPixel(Px, Py, GammaBlend(Dst, Color, Alpha));
            }
        }
    }
}

static void PaintGlyph(UINT32 X, UINT32 Y, const UINT8 *Glyph, UINT32 Width,
                       UINT32 Height, UINT32 Bpr, UINT32 ScaleX, UINT32 ScaleY,
                       UINT32 OffY, UINT32 Color) {
    UINT32 Row;
    UINT32 Col;

    if (!Glyph || Width == 0 || Height == 0 || Bpr == 0) {
        return;
    }
    if (ScaleX < 1) {
        ScaleX = 1;
    }
    if (ScaleY < 1) {
        ScaleY = 1;
    }
    for (Row = 0; Row < Height; Row++) {
        for (Col = 0; Col < Width; Col++) {
            UINT32 Dx = X + Col * ScaleX;
            UINT32 Dy = Y + OffY + Row * ScaleY;
            UINT8 Alpha;

            if (GlyphBit(Glyph, Bpr, Width, Height, (INT32)Col, (INT32)Row)) {
                PaintBlock(Dx, Dy, ScaleX, ScaleY, Color, 255);
                continue;
            }
            if (!gGlyphSmooth) {
                continue;
            }
            Alpha = EdgeAlpha(Glyph, Bpr, Width, Height, Col, Row);
            if (Alpha != 0) {
                PaintBlock(Dx, Dy, ScaleX, ScaleY, Color, Alpha);
            }
        }
    }
}

static UINT8 GlyphNibble4(const UINT8 *Glyph, UINT32 Bpr, UINT32 Width,
                          UINT32 Height, UINT32 Col, UINT32 Row) {
    UINT8 Byte;

    if (Col >= Width || Row >= Height || !Glyph || Bpr == 0) {
        return 0;
    }
    Byte = Glyph[Row * Bpr + (Col / 2u)];
    if ((Col & 1u) == 0) {
        return (UINT8)((Byte >> 4) & 0xFu);
    }
    return (UINT8)(Byte & 0xFu);
}

/* PR-UI-cjk-crisp：淡灰当透明、深灰实心，去掉 18px AA 光晕 */
static UINT8 GlyphCrispAlpha4(UINT8 N) {
    if (N <= 4u) {
        return 0;
    }
    if (N >= 10u) {
        return 255;
    }
    return (UINT8)(N * 17u);
}

/* PR-UI-cjk-gray：每像素 4bit（0..15）；crisp 后再 blend */
static void PaintGlyph4(UINT32 X, UINT32 Y, const UINT8 *Glyph, UINT32 Width,
                        UINT32 Height, UINT32 Bpr, UINT32 ScaleX, UINT32 ScaleY,
                        UINT32 OffY, UINT32 Color) {
    UINT32 Row;
    UINT32 Col;

    if (!Glyph || Width == 0 || Height == 0 || Bpr == 0) {
        return;
    }
    if (ScaleX < 1) {
        ScaleX = 1;
    }
    if (ScaleY < 1) {
        ScaleY = 1;
    }
    for (Row = 0; Row < Height; Row++) {
        for (Col = 0; Col < Width; Col++) {
            UINT8 N = GlyphNibble4(Glyph, Bpr, Width, Height, Col, Row);
            UINT8 Alpha;
            UINT32 Dx;
            UINT32 Dy;

            Alpha = GlyphCrispAlpha4(N);
            if (Alpha == 0) {
                continue;
            }
            Dx = X + Col * ScaleX;
            Dy = Y + OffY + Row * ScaleY;
            PaintBlock(Dx, Dy, ScaleX, ScaleY, Color, Alpha);
        }
    }
}

/*
 * 保留字库给出的灰度。以前把 190 以上收成实心：黑字边缘变成黑块，
 * 灰字因为颜色本身浅，看起来反而更顺。
 */
static UINT8 GlyphCrispAlpha8(UINT8 N) {
    if (N < 16u) {
        return 0;
    }
    return N;
}

/* PR-UI-ttf-2：stb 8bpp 灰图；淡边丢、深灰实心 */
static void PaintGlyph8(UINT32 X, UINT32 Y, const UINT8 *Glyph, UINT32 Width,
                        UINT32 Height, UINT32 Bpr, UINT32 ScaleX, UINT32 ScaleY,
                        UINT32 OffY, UINT32 Color) {
    UINT32 Row;
    UINT32 Col;

    if (!Glyph || Width == 0 || Height == 0 || Bpr == 0) {
        return;
    }
    if (ScaleX < 1) {
        ScaleX = 1;
    }
    if (ScaleY < 1) {
        ScaleY = 1;
    }
    for (Row = 0; Row < Height; Row++) {
        for (Col = 0; Col < Width; Col++) {
            UINT8 N = Glyph[Row * Bpr + Col];
            UINT8 Alpha;
            UINT32 Dx;
            UINT32 Dy;

            Alpha = GlyphCrispAlpha8(N);
            if (Alpha == 0) {
                continue;
            }
            Dx = X + Col * ScaleX;
            Dy = Y + OffY + Row * ScaleY;
            PaintBlock(Dx, Dy, ScaleX, ScaleY, Color, Alpha);
        }
    }
}

static void PaintGlyphFitted(UINT32 X, UINT32 Y, const UINT8 *Glyph,
                             UINT32 SrcW, UINT32 SrcH, UINT32 DstW, UINT32 DstH,
                             UINT32 Bpp, UINT32 Color);

void VideoDrawCharAt(UINT32 X, UINT32 Y, char C, UINT32 Color) {
    const FONT_FACE *F;
    const UINT8 *Glyph;
    UINT32 Scale;
    UINT32 NatW;
    UINT32 NatH;
    UINT32 Dw;
    UINT32 Dh;

    F = FontGetCurrent();
    Glyph = FontGlyph(C);
    if (F == 0 || Glyph == 0) {
        return;
    }
    Scale = F->Scale ? F->Scale : 1u;
    NatW = F->Width * Scale;
    NatH = F->Height * Scale;
    Dw = FontCellW();
    Dh = FontCellH();
    if (Dw == NatW && Dh == NatH) {
        PaintGlyph(X, Y, Glyph, F->Width, F->Height, F->BytesPerRow, Scale, Scale,
                   0, Color);
        return;
    }
    /* 和汉字同一高度。10×18 收到 24 不是整数倍，按目标格子取样。 */
    PaintGlyphFitted(X, Y, Glyph, F->Width, F->Height, Dw, Dh, 1u, Color);
}

/* 字形比行高还高时，整字收到这一行，不再只裁掉下半截。 */
static void PaintGlyphFitted(UINT32 X, UINT32 Y, const UINT8 *Glyph,
                             UINT32 SrcW, UINT32 SrcH, UINT32 DstW, UINT32 DstH,
                             UINT32 Bpp, UINT32 Color) {
    UINT32 Row;
    UINT32 Col;
    UINT32 Bpr4;
    UINT32 Bpr1;

    if (!Glyph || SrcW == 0 || SrcH == 0 || DstW == 0 || DstH == 0) {
        return;
    }
    Bpr4 = (SrcW + 1u) / 2u;
    Bpr1 = (SrcW + 7u) / 8u;
    for (Row = 0; Row < DstH; Row++) {
        UINT32 Sy0 = (Row * SrcH) / DstH;
        UINT32 Sy1 = ((Row + 1u) * SrcH) / DstH;
        UINT32 Sy;

        if (Sy1 <= Sy0) {
            Sy1 = Sy0 + 1u;
        }
        if (Sy1 > SrcH) {
            Sy1 = SrcH;
        }
        for (Col = 0; Col < DstW; Col++) {
            UINT32 Sx0 = (Col * SrcW) / DstW;
            UINT32 Sx1 = ((Col + 1u) * SrcW) / DstW;
            UINT32 Sx;
            UINT8 Alpha = 0;

            if (Sx1 <= Sx0) {
                Sx1 = Sx0 + 1u;
            }
            if (Sx1 > SrcW) {
                Sx1 = SrcW;
            }
            for (Sy = Sy0; Sy < Sy1 && Alpha < 255u; Sy++) {
                for (Sx = Sx0; Sx < Sx1; Sx++) {
                    UINT8 Sample;

                    if (Bpp == 8u) {
                        Sample = GlyphCrispAlpha8(Glyph[Sy * SrcW + Sx]);
                    } else if (Bpp == 4u) {
                        Sample = GlyphCrispAlpha4(
                            GlyphNibble4(Glyph, Bpr4, SrcW, SrcH, Sx, Sy));
                    } else {
                        Sample = GlyphBit(Glyph, Bpr1, SrcW, SrcH, (INT32)Sx,
                                          (INT32)Sy)
                                     ? 255u
                                     : 0u;
                    }
                    if (Sample > Alpha) {
                        Alpha = Sample;
                    }
                }
            }
            if (Alpha != 0u) {
                PaintBlock(X + Col, Y + Row, 1u, 1u, Color, Alpha);
            }
        }
    }
}

/* 点阵：1bpp 或 4bpp（CJK gray）；BytesPerRow 随 bpp */
static void VideoDrawBitmapAt(UINT32 X, UINT32 Y, const UINT8 *Glyph,
                              UINT32 Width, UINT32 Height, UINT32 Color,
                              UINT32 Bpp) {
    UINT32 ScaleX;
    UINT32 ScaleY;
    UINT32 CellH;
    UINT32 OffY;
    UINT32 DrawnH;
    UINT32 BoxW;
    UINT32 BoxH;
    UINT32 Bpr;

    if (!Glyph || Width == 0 || Height == 0) {
        return;
    }
    ScaleY = FontGlyphStretch(Height);
    ScaleX = ScaleY;
    if (ScaleX < 1) {
        ScaleX = 1;
    }
    if (ScaleY < 1) {
        ScaleY = 1;
    }
    CellH = FontCellH();
    FontGlyphBox(Width, Height, &BoxW, &BoxH);
    OffY = 0;
    if (BoxH != Height * ScaleY || BoxW != Width * ScaleX) {
        PaintGlyphFitted(X, Y, Glyph, Width, Height, BoxW, BoxH, Bpp, Color);
        return;
    }
    ScaleX = ScaleY;
    DrawnH = Height * ScaleY;
    if (DrawnH > CellH) {
        UINT32 Up = (DrawnH - CellH) / 2u;

        if (Y > Up) {
            Y -= Up;
        } else {
            Y = 0;
        }
    } else if (CellH > DrawnH) {
        OffY = (CellH - DrawnH) / 2;
    }
    if (Bpp == 8) {
        PaintGlyph8(X, Y, Glyph, Width, Height, Width, ScaleX, ScaleY, OffY, Color);
        return;
    }
    if (Bpp == 4) {
        Bpr = (Width + 1u) / 2u;
        PaintGlyph4(X, Y, Glyph, Width, Height, Bpr, ScaleX, ScaleY, OffY, Color);
        return;
    }
    Bpr = (Width + 7u) / 8u;
    PaintGlyph(X, Y, Glyph, Width, Height, Bpr, ScaleX, ScaleY, OffY, Color);
}

/* PR-UI-cjk：缺字空心框，边长跟当前行里的汉字一样 */
static void VideoDrawMissingGlyphBox(UINT32 X, UINT32 Y, UINT32 Color) {
    UINT32 Native = FontCjkDim();
    UINT32 W;
    UINT32 H;
    UINT32 OffY;
    UINT32 CellH;
    UINT32 Col;
    UINT32 Row;
    UINT32 Dy;

    FontGlyphBox(Native, Native, &W, &H);
    CellH = FontCellH();
    OffY = 0;
    if (CellH > H) {
        OffY = (CellH - H) / 2;
    }
    for (Col = 0; Col < W; Col++) {
        VideoDrawPixel(X + Col, Y + OffY, Color);
        if (H > 0) {
            VideoDrawPixel(X + Col, Y + OffY + H - 1, Color);
        }
    }
    for (Row = 0; Row < H; Row++) {
        Dy = Y + OffY + Row;
        VideoDrawPixel(X, Dy, Color);
        if (W > 0) {
            VideoDrawPixel(X + W - 1, Dy, Color);
        }
    }
}

void VideoDrawCodepointAt(UINT32 X, UINT32 Y, UINT32 Cp, UINT32 Color) {
    UINT32 W;
    UINT32 H;
    const UINT8 *G;

    if (Cp < 128) {
        VideoDrawCharAt(X, Y, (char)Cp, Color);
        return;
    }
    G = FontGlyphCp(Cp, &W, &H);
    if (!G) {
        if (FontGlyphCpDeferred()) {
            return;
        }
        VideoDrawMissingGlyphBox(X, Y, Color);
        return;
    }
    VideoDrawBitmapAt(X, Y, G, W, H, Color, FontGlyphCpBpp());
}

void VideoDrawStringAt(UINT32 X, UINT32 Y, const char *Text, UINT32 Color) {
    UINT32 CurX = X;
    UINT32 AdvY = FontAdvanceY();

    while (Text && *Text) {
        UINT32 Cp;
        UINTN N;

        if (*Text == '\n') {
            CurX = X;
            Y += AdvY;
            Text++;
            continue;
        }
        N = Utf8Decode(Text, &Cp);
        if (N == 0) {
            Text++;
            continue;
        }
        VideoDrawCodepointAt(CurX, Y, Cp, Color);
        CurX += CodepointAdvance(Cp);
        Text += N;
    }
}
