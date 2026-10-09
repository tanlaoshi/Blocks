/*
 * Font.c — 画字积木（Terminus 10×18 + 盘读 CJK 18×18×4bpp）
 *
 * ASCII = Terminus 10×18；汉字 = CJK32.BIN（默认 18×18×4bpp）或内建 CJK16；
 * TTF 仅补缺字。
 */
#include "Font.h"
#include "FontTerminus10x18.h"
#include "FontCjk16.h"
#include "FontCjkDisk.h"
#include "FontTtf.h"
#include "Utf8.h"
#include "HalFpu.h"
#include "HalSerial.h"
#include "HalVideo.h"
#include "ToySerialConfig.h"

static int gFontReady;

void FontInitialize(void) {
    if (gFontReady) {
        return;
    }
    gFontReady = 1;
    HalSerialWriteChannel(TOY_SLOG_GUI, "Font: terminus10x18 ok\n");
    if (FontCjkDiskLoad() != 0) {
        HalSerialWriteChannel(TOY_SLOG_GUI, "Font: cjk fallback 16×1bpp\n");
    }
#if defined(__x86_64__) || defined(_M_X64)
    if (HalFpuSelfTest() != 0) {
        HalSerialWriteChannel(TOY_SLOG_GUI, "Font: fpu skip\n");
    } else if (FontTtfLoad() != 0) {
        /* miss */
    } else if (FontTtfInit() != 0) {
        /* skip */
    } else {
        HalSerialWriteChannel(TOY_SLOG_GUI, "Font: ttf fallback ready\n");
    }
#endif
}

UINT32 FontCellWidth(void) {
    return FONT_TERM10_W;
}

UINT32 FontCellHeight(void) {
    UINT32 Cjk = FontCjkCell();
    return (Cjk > FONT_TERM10_H) ? Cjk : FONT_TERM10_H;
}

UINT32 FontCjkCell(void) {
    if (FontCjkDiskReady()) {
        return FontCjkDiskDim();
    }
    return FONT_CJK16_DIM;
}

static void DrawAsciiAt(UINT32 X, UINT32 Y, UINT32 Cp, UINT32 Color) {
    const UINT8 *Bits = FontTerminus10Glyph(Cp);
    UINT32 Row;
    UINT32 Col;
    UINT32 Oy = 0;
    UINT32 LineH = FontCellHeight();

    if (LineH > FONT_TERM10_H) {
        Oy = (LineH - FONT_TERM10_H) / 2u;
    }

    for (Row = 0; Row < FONT_TERM10_H; Row++) {
        UINT8 B0 = Bits[Row * 2u];
        UINT8 B1 = Bits[Row * 2u + 1u];
        for (Col = 0; Col < FONT_TERM10_W; Col++) {
            UINT8 Byte = (Col < 8u) ? B0 : B1;
            int Bit = 7 - (int)(Col % 8u);
            if (Byte & (UINT8)(1u << Bit)) {
                HalVideoDrawPixel(X + Col, Y + Oy + Row, Color);
            }
        }
    }
}

static void DrawMissingBox(UINT32 X, UINT32 Y, UINT32 Color) {
    UINT32 i;
    UINT32 Dim = FontCjkCell();
    for (i = 0; i < Dim; i++) {
        HalVideoDrawPixel(X + i, Y, Color);
        HalVideoDrawPixel(X + i, Y + Dim - 1u, Color);
        HalVideoDrawPixel(X, Y + i, Color);
        HalVideoDrawPixel(X + Dim - 1u, Y + i, Color);
    }
}

static UINT32 BlendRgb(UINT32 Fg, UINT32 Bg, UINT32 A) {
    UINT32 Fr = (Fg >> 16) & 0xFFu;
    UINT32 Fg_ = (Fg >> 8) & 0xFFu;
    UINT32 Fb = Fg & 0xFFu;
    UINT32 Br = (Bg >> 16) & 0xFFu;
    UINT32 Bg_ = (Bg >> 8) & 0xFFu;
    UINT32 Bb = Bg & 0xFFu;
    UINT32 R = (Fr * A + Br * (255u - A)) / 255u;
    UINT32 G = (Fg_ * A + Bg_ * (255u - A)) / 255u;
    UINT32 B = (Fb * A + Bb * (255u - A)) / 255u;
    return (R << 16) | (G << 8) | B;
}

static UINT8 CrispAlpha4(UINT8 N) {
    if (N <= 4u) {
        return 0;
    }
    if (N >= 10u) {
        return 255;
    }
    return (UINT8)(N * 17u);
}

static void DrawCjk4At(UINT32 X, UINT32 Y, const UINT8 *Glyph, UINT32 Dim,
                       UINT32 Color) {
    UINT32 Row;
    UINT32 Col;
    UINT32 Bpr = (Dim + 1u) / 2u;
    UINT32 LineH = FontCellHeight();
    UINT32 Oy = (LineH > Dim) ? ((LineH - Dim) / 2u) : 0;

    for (Row = 0; Row < Dim; Row++) {
        for (Col = 0; Col < Dim; Col++) {
            UINT8 Byte = Glyph[Row * Bpr + (Col / 2u)];
            UINT8 N = ((Col & 1u) == 0) ? (UINT8)((Byte >> 4) & 0xFu)
                                        : (UINT8)(Byte & 0xFu);
            UINT8 A = CrispAlpha4(N);
            UINT32 Bg;
            if (A == 0) {
                continue;
            }
            if (A >= 240u) {
                HalVideoDrawPixel(X + Col, Y + Oy + Row, Color & 0x00FFFFFFu);
                continue;
            }
            Bg = HalVideoReadPixel(X + Col, Y + Oy + Row);
            HalVideoDrawPixel(X + Col, Y + Oy + Row,
                              BlendRgb(Color & 0x00FFFFFFu, Bg, A));
        }
    }
}

static void DrawCjk1At(UINT32 X, UINT32 Y, const UINT8 *Bits, UINT32 Color) {
    UINT32 Row;
    UINT32 Col;
    UINT32 LineH = FontCellHeight();
    UINT32 Oy = (LineH > FONT_CJK16_DIM) ? ((LineH - FONT_CJK16_DIM) / 2u) : 0;

    for (Row = 0; Row < FONT_CJK16_DIM; Row++) {
        UINT8 B0 = Bits[Row * 2u];
        UINT8 B1 = Bits[Row * 2u + 1u];
        for (Col = 0; Col < 8u; Col++) {
            if (B0 & (UINT8)(0x80u >> Col)) {
                HalVideoDrawPixel(X + Col, Y + Oy + Row, Color);
            }
        }
        for (Col = 0; Col < 8u; Col++) {
            if (B1 & (UINT8)(0x80u >> Col)) {
                HalVideoDrawPixel(X + 8u + Col, Y + Oy + Row, Color);
            }
        }
    }
}

static void DrawTtfAt(UINT32 X, UINT32 Y, const UINT8 *Pix, UINT32 Color) {
    UINT32 Row;
    UINT32 Col;
    UINT32 Cell = 18u; /* 与 FontTtfCache 一致（18） */
    UINT32 LineH = FontCellHeight();
    UINT32 Oy = (LineH > Cell) ? ((LineH - Cell) / 2u) : 0;

    for (Row = 0; Row < Cell; Row++) {
        for (Col = 0; Col < Cell; Col++) {
            UINT32 A = Pix[Row * Cell + Col];
            UINT32 Bg;
            if (A == 0) {
                continue;
            }
            if (A >= 240u) {
                HalVideoDrawPixel(X + Col, Y + Oy + Row, Color & 0x00FFFFFFu);
                continue;
            }
            Bg = HalVideoReadPixel(X + Col, Y + Oy + Row);
            HalVideoDrawPixel(X + Col, Y + Oy + Row,
                              BlendRgb(Color & 0x00FFFFFFu, Bg, A));
        }
    }
}

void FontDrawCodepointAt(UINT32 X, UINT32 Y, UINT32 Cp, UINT32 Color) {
    const UINT8 *Bits;
    UINT32 Bytes = 0;

    if (!gFontReady) {
        FontInitialize();
    }
    if (Cp < 0x80u) {
        DrawAsciiAt(X, Y, Cp, Color);
        return;
    }
    Bits = FontCjkDiskLookup(Cp, &Bytes);
    if (Bits != 0) {
        DrawCjk4At(X, Y, Bits, FontCjkDiskDim(), Color);
        return;
    }
    Bits = FontCjk16Lookup(Cp);
    if (Bits != 0) {
        DrawCjk1At(X, Y, Bits, Color);
        return;
    }
    {
        const UINT8 *Ttf = FontTtfCacheGet(Cp);
        if (Ttf) {
            DrawTtfAt(X, Y, Ttf, Color);
            return;
        }
    }
    DrawMissingBox(X, Y, Color);
}

void FontDrawCharAt(UINT32 X, UINT32 Y, char C, UINT32 Color) {
    FontDrawCodepointAt(X, Y, (UINT32)(UINT8)C, Color);
}

void FontDrawStringAt(UINT32 X, UINT32 Y, const char *Text, UINT32 Color) {
    UINT32 Cursor = X;

    if (!gFontReady) {
        FontInitialize();
    }
    if (Text == 0) {
        return;
    }
    while (*Text) {
        UINT32 Cp;
        UINTN N;

        if (*Text == '\n') {
            Cursor = X;
            Y += FontCellHeight();
            Text++;
            continue;
        }
        N = Utf8Decode(Text, &Cp);
        if (N == 0) {
            Text++;
            continue;
        }
        FontDrawCodepointAt(Cursor, Y, Cp, Color);
        Cursor += (Cp < 0x80u) ? FontCellWidth() : FontCjkCell();
        Text += N;
    }
}

void FontDrawChar(char C, UINT32 Color) {
    FontDrawCharAt(0, 0, C, Color);
}

void FontDrawString(const char *Text, UINT32 Color) {
    FontDrawStringAt(0, 0, Text, Color);
}
