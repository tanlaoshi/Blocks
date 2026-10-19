/*
 * BootLogo.c — GOP 设好后画出 Blocks。
 * T 四格橙色悬在上面，凹五格青色在下。读盘时 T 下落。
 * 落到进度 100 时，竖条填进凹口，变成三行、每行三块，中间没有空位。
 * 凹形不动。格子尺寸与内核相同。
 * 只有矩形填充和 5×7 点阵，不依赖内核字体。
 */
#include "BootPrivate.h"

#define GLYPH_W 5
#define GLYPH_H 7
#define WORD_SCALE 8
#define CELL 48
#define GAP 8
#define OPEN_MAX 112
#define STEP (CELL + GAP)

/* 每字 7 行，高位在左，只用低 5 位。 */
static const UINT8 gGlyphs[][GLYPH_H] = {
    { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E }, /* B */
    { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F }, /* l */
    { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, /* o */
    { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E }, /* c */
    { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 }, /* k */
    { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E }, /* s */
};

static EFI_GRAPHICS_OUTPUT_PROTOCOL *sGop;
static UINTN sAx;
static UINTN sAy;
static UINTN sMarkW;
static UINTN sMarkH;
static UINTN sPct = 101;

static void Fill(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop,
                 UINTN X, UINTN Y, UINTN W, UINTN H,
                 UINT8 R, UINT8 G, UINT8 B) {
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL Px;

    if (Gop == NULL || W == 0 || H == 0) {
        return;
    }
    Px.Red = R;
    Px.Green = G;
    Px.Blue = B;
    Px.Reserved = 0;
    (void)Gop->Blt(Gop, &Px, EfiBltVideoFill, 0, 0, X, Y, W, H, 0);
}

static void Glyph(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop,
                  UINTN X, UINTN Y, const UINT8 *Rows,
                  UINT8 R, UINT8 G, UINT8 B) {
    UINTN Row;
    UINTN Col;

    for (Row = 0; Row < GLYPH_H; Row++) {
        for (Col = 0; Col < GLYPH_W; Col++) {
            if ((Rows[Row] & (1u << (GLYPH_W - 1 - Col))) == 0) {
                continue;
            }
            Fill(Gop, X + Col * WORD_SCALE, Y + Row * WORD_SCALE,
                 WORD_SCALE, WORD_SCALE, R, G, B);
        }
    }
}

static void PaintPieces(UINTN Pct) {
    UINTN Shift;
    UINTN X;
    UINTN Yt;
    UINTN Yu;

    if (sGop == NULL) {
        return;
    }
    if (Pct > 100) {
        Pct = 100;
    }
    Shift = (100 - Pct) * OPEN_MAX / 100;
    X = sAx;
    Yt = (sAy > Shift) ? (sAy - Shift) : 0;
    Yu = sAy;
    Fill(sGop, sAx, (sAy > OPEN_MAX) ? (sAy - OPEN_MAX) : 0,
         sMarkW, sMarkH + OPEN_MAX, 0x10, 0x18, 0x28);
    Fill(sGop, X, Yt, CELL, CELL, 0xF0, 0xA0, 0x30);
    Fill(sGop, X + STEP, Yt, CELL, CELL, 0xF0, 0xA0, 0x30);
    Fill(sGop, X + 2 * STEP, Yt, CELL, CELL, 0xF0, 0xA0, 0x30);
    Fill(sGop, X + STEP, Yt + STEP, CELL, CELL, 0xF0, 0xA0, 0x30);
    /* 凹口在第二行正中。T 的竖条落下后占住这个空位，九格排满。 */
    Fill(sGop, X, Yu + STEP, CELL, CELL, 0x2E, 0xC4, 0xB6);
    Fill(sGop, X + 2 * STEP, Yu + STEP, CELL, CELL, 0x2E, 0xC4, 0xB6);
    Fill(sGop, X, Yu + 2 * STEP, CELL, CELL, 0x2E, 0xC4, 0xB6);
    Fill(sGop, X + STEP, Yu + 2 * STEP, CELL, CELL, 0x2E, 0xC4, 0xB6);
    Fill(sGop, X + 2 * STEP, Yu + 2 * STEP, CELL, CELL, 0x2E, 0xC4, 0xB6);
    sPct = Pct;
}

void BootLogoProgress(UINTN Pct) {
    if (sGop == NULL || Pct <= sPct) {
        return;
    }
    PaintPieces(Pct);
}

void BootShowLogo(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop) {
    UINTN W;
    UINTN H;
    UINTN Span;
    UINTN TextW;
    UINTN X;
    UINTN Y;
    UINTN i;
    static const UINT8 *Word[6] = {
        gGlyphs[0], gGlyphs[1], gGlyphs[2],
        gGlyphs[3], gGlyphs[4], gGlyphs[5],
    };

    if (Gop == NULL || Gop->Mode == NULL || Gop->Mode->Info == NULL) {
        return;
    }
    W = Gop->Mode->Info->HorizontalResolution;
    H = Gop->Mode->Info->VerticalResolution;
    if (W < 320 || H < 200) {
        return;
    }

    sGop = Gop;
    sMarkW = CELL * 3 + GAP * 2;
    sMarkH = CELL * 3 + GAP * 2;
    Span = sMarkH + OPEN_MAX + 20 + GLYPH_H * WORD_SCALE;
    sAx = (W > sMarkW) ? (W - sMarkW) / 2 : 0;
    sAy = (H > Span) ? ((H - Span) / 2 + OPEN_MAX) : OPEN_MAX;

    Fill(Gop, 0, 0, W, H, 0x10, 0x18, 0x28);
    TextW = 6 * (GLYPH_W * WORD_SCALE) + 5 * WORD_SCALE;
    X = (W > TextW) ? (W - TextW) / 2 : 0;
    Y = sAy + sMarkH + 20;
    for (i = 0; i < 6; i++) {
        Glyph(Gop, X, Y, Word[i], 0xE8, 0xEE, 0xF4);
        X += (GLYPH_W * WORD_SCALE) + WORD_SCALE;
    }
    sPct = 101;
    PaintPieces(0);
    BootSerialPrintf("Boot: Logo\n");
}
