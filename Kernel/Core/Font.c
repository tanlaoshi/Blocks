/*
 * Font.c — 画字积木（ASCII/CJK 点阵 + K27 TTF 优先）
 *
 * ASCII 仍 16×16 点阵。汉字：有 TTF 走 8bpp，否则 CJK16；缺字画「□」。
 */
#include "Font.h"
#include "FontAscii16.h"
#include "FontCjk16.h"
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
    HalSerialWriteChannel(TOY_SLOG_GUI, "Font: ascii16+cjk16 ok\n");
#if defined(__x86_64__) || defined(_M_X64)
    if (HalFpuSelfTest() != 0) {
        HalSerialWriteChannel(TOY_SLOG_GUI, "Font: fpu skip\n");
    } else if (FontTtfLoad() != 0) {
        /* Load 已打 miss/oom */
    } else if (FontTtfInit() != 0) {
        /* Init 已打 skip/fail */
    } else {
        HalSerialWriteChannel(TOY_SLOG_GUI, "Font: ttf ready\n");
    }
#endif
}

UINT32 FontCellWidth(void) {
    return FONT_ASCII16_DIM;
}

UINT32 FontCellHeight(void) {
    return FONT_CJK16_DIM;
}

UINT32 FontCjkCell(void) {
    return FONT_CJK16_DIM;
}

static void DrawAsciiAt(UINT32 X, UINT32 Y, UINT32 Cp, UINT32 Color) {
    const UINT8 *Bits = FontAscii16Glyph(Cp);
    UINT32 Row;
    UINT32 Col;

    for (Row = 0; Row < FONT_ASCII16_DIM; Row++) {
        UINT8 B0 = Bits[Row * 2u];
        UINT8 B1 = Bits[Row * 2u + 1u];
        for (Col = 0; Col < 8u; Col++) {
            if (B0 & (UINT8)(0x80u >> Col)) {
                HalVideoDrawPixel(X + Col, Y + Row, Color);
            }
        }
        for (Col = 0; Col < 8u; Col++) {
            if (B1 & (UINT8)(0x80u >> Col)) {
                HalVideoDrawPixel(X + 8u + Col, Y + Row, Color);
            }
        }
    }
}

static void DrawMissingBox(UINT32 X, UINT32 Y, UINT32 Color) {
    UINT32 i;
    for (i = 0; i < FONT_CJK16_DIM; i++) {
        HalVideoDrawPixel(X + i, Y, Color);
        HalVideoDrawPixel(X + i, Y + FONT_CJK16_DIM - 1u, Color);
        HalVideoDrawPixel(X, Y + i, Color);
        HalVideoDrawPixel(X + FONT_CJK16_DIM - 1u, Y + i, Color);
    }
}

static void DrawCjkAt(UINT32 X, UINT32 Y, const UINT8 *Bits, UINT32 Color) {
    UINT32 Row;
    UINT32 Col;

    for (Row = 0; Row < FONT_CJK16_DIM; Row++) {
        UINT8 B0 = Bits[Row * 2u];
        UINT8 B1 = Bits[Row * 2u + 1u];
        for (Col = 0; Col < 8u; Col++) {
            if (B0 & (UINT8)(0x80u >> Col)) {
                HalVideoDrawPixel(X + Col, Y + Row, Color);
            }
        }
        for (Col = 0; Col < 8u; Col++) {
            if (B1 & (UINT8)(0x80u >> Col)) {
                HalVideoDrawPixel(X + 8u + Col, Y + Row, Color);
            }
        }
    }
}

static void DrawTtfAt(UINT32 X, UINT32 Y, const UINT8 *Pix, UINT32 Color) {
    UINT32 Row;
    UINT32 Col;
    /* 缓存已是硬边 0/255；阈值防脏数据 */
    for (Row = 0; Row < FONT_CJK16_DIM; Row++) {
        for (Col = 0; Col < FONT_CJK16_DIM; Col++) {
            if (Pix[Row * FONT_CJK16_DIM + Col] >= 128u) {
                HalVideoDrawPixel(X + Col, Y + Row, Color);
            }
        }
    }
}

void FontDrawCodepointAt(UINT32 X, UINT32 Y, UINT32 Cp, UINT32 Color) {
    const UINT8 *Bits;

    if (!gFontReady) {
        FontInitialize();
    }
    if (Cp < 0x80u) {
        DrawAsciiAt(X, Y, Cp, Color);
        return;
    }
    {
        const UINT8 *Ttf = FontTtfCacheGet(Cp);
        if (Ttf) {
            DrawTtfAt(X, Y, Ttf, Color);
            return;
        }
    }
    Bits = FontCjk16Lookup(Cp);
    if (Bits == 0) {
        DrawMissingBox(X, Y, Color);
        return;
    }
    DrawCjkAt(X, Y, Bits, Color);
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
            Y += FONT_CJK16_DIM;
            Text++;
            continue;
        }
        N = Utf8Decode(Text, &Cp);
        if (N == 0) {
            Text++;
            continue;
        }
        FontDrawCodepointAt(Cursor, Y, Cp, Color);
        Cursor += (Cp < 0x80u) ? FONT_ASCII16_DIM : FONT_CJK16_DIM;
        Text += N;
    }
}

void FontDrawChar(char C, UINT32 Color) {
    FontDrawCharAt(0, 0, C, Color);
}

void FontDrawString(const char *Text, UINT32 Color) {
    FontDrawStringAt(0, 0, Text, Color);
}
