/*
 * BootLogo.c — GOP 设好后画出 Blocks。
 * T 四格橙色悬在上面，凹五格青色在下。读盘时 T 下落。
 * 落到进度 100 时，竖条填进凹口，变成三行、每行三块，中间没有空位。
 * 凹形不动。格子尺寸与内核相同。画面上只有色块。
 */
#include "BootPrivate.h"

#define CELL 48
#define GAP 8
#define OPEN_MAX 112
#define STEP (CELL + GAP)

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
    Fill(sGop, X, Yt, CELL, CELL, 0xC0, 0x78, 0x20);
    Fill(sGop, X + STEP, Yt, CELL, CELL, 0xC0, 0x78, 0x20);
    Fill(sGop, X + 2 * STEP, Yt, CELL, CELL, 0xC0, 0x78, 0x20);
    Fill(sGop, X + STEP, Yt + STEP, CELL, CELL, 0xC0, 0x78, 0x20);
    /* 凹口在第二行正中。T 的竖条落下后占住这个空位，九格排满。 */
    Fill(sGop, X, Yu + STEP, CELL, CELL, 0x1E, 0x9A, 0x8E);
    Fill(sGop, X + 2 * STEP, Yu + STEP, CELL, CELL, 0x1E, 0x9A, 0x8E);
    Fill(sGop, X, Yu + 2 * STEP, CELL, CELL, 0x1E, 0x9A, 0x8E);
    Fill(sGop, X + STEP, Yu + 2 * STEP, CELL, CELL, 0x1E, 0x9A, 0x8E);
    Fill(sGop, X + 2 * STEP, Yu + 2 * STEP, CELL, CELL, 0x1E, 0x9A, 0x8E);
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
    /* 合成后的九格居中。T 从这块上方落下，不把悬空高度算进居中。 */
    Span = sMarkH;
    sAx = (W > sMarkW) ? (W - sMarkW) / 2 : 0;
    sAy = (H > Span) ? (H - Span) / 2 : 0;

    Fill(Gop, 0, 0, W, H, 0x10, 0x18, 0x28);
    sPct = 101;
    PaintPieces(0);
    BootSerialPrintf("Boot: Logo\n");
}
