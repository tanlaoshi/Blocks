/*
 * Font.c — 画字积木（K21；自 Hal/Common/HalFont.c 收回）
 *
 * 【初学者】
 * 每个可打印字符是 8 行，每行 1 字节；bit7 = 最左像素。
 * 画字 = 按位调 HalVideoDrawPixel（无 TTF、无平滑、无 UTF-8）。
 *
 * 对外契约仍是 HalVideoDrawString*（声明在 HalVideo.h），实现落在本积木。
 */
#include "Font.h"
#include "FontGlyph8x8.h"
#include "HalSerial.h"
#include "HalVideo.h"
#include "ToySerialConfig.h"

static int gFontReady;

void FontInitialize(void) {
    if (gFontReady) {
        return;
    }
    gFontReady = 1;
    HalSerialWriteChannel(TOY_SLOG_GUI, "Font: 8x8 ok\n");
}

UINT32 FontCellWidth(void) {
    return FONT_CELL_W;
}

UINT32 FontCellHeight(void) {
    return FONT_CELL_H;
}

void HalVideoDrawCharAt(UINT32 X, UINT32 Y, char C, UINT32 Color) {
    UINT32 Cp = (UINT32)(UINT8)C;
    UINT32 Row;
    UINT32 Col;
    const UINT8 *Glyph;
    UINT32 Index;

    if (!gFontReady) {
        FontInitialize();
    }
    if (Cp < FONT_GLYPH_FIRST || Cp >= FONT_GLYPH_FIRST + FONT_GLYPH_COUNT) {
        Cp = (UINT32)'?';
    }
    Index = Cp - FONT_GLYPH_FIRST;
    Glyph = gFontGlyph8x8[Index];
    for (Row = 0; Row < FONT_CELL_H; Row++) {
        UINT8 Bits = Glyph[Row];
        for (Col = 0; Col < FONT_CELL_W; Col++) {
            if (Bits & (UINT8)(0x80u >> Col)) {
                HalVideoDrawPixel(X + Col, Y + Row, Color);
            }
        }
    }
}

void HalVideoDrawStringAt(UINT32 X, UINT32 Y, const char *Text, UINT32 Color) {
    UINT32 Cursor = X;

    if (!gFontReady) {
        FontInitialize();
    }
    if (Text == 0) {
        return;
    }
    while (*Text) {
        char Ch = *Text++;
        if (Ch == '\n') {
            Cursor = X;
            Y += FONT_CELL_H;
            continue;
        }
        HalVideoDrawCharAt(Cursor, Y, Ch, Color);
        Cursor += FONT_CELL_W;
    }
}

void HalVideoDrawChar(char C, UINT32 Color) {
    HalVideoDrawCharAt(0, 0, C, Color);
}

void HalVideoDrawString(const char *Text, UINT32 Color) {
    HalVideoDrawStringAt(0, 0, Text, Color);
}
