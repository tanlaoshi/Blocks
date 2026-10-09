/*
 * HalFont.c — HalVideo 画字后端（K2 起；自 Core/Font.c 迁入）
 *
 * 【初学者】
 * 每个可打印字符是 8 行，每行 1 字节；bit7 = 最左像素。
 * 画字 = 按位调 HalVideoDrawPixel（无 Theme、无平滑、无 UTF-8）。
 *
 * 【落点】先挂 Hal（与 Video 一家）；完整 Theme/TTF 迁入时再拆成 Font 积木，
 * 契约仍走 HalVideoDrawString*（见 积木原则 / Kernel迁移 修订记录）。
 */
#include "HalVideo.h"
#include "FontGlyph8x8.h"

void HalVideoDrawCharAt(UINT32 X, UINT32 Y, char C, UINT32 Color) {
    UINT32 Cp = (UINT32)(UINT8)C;
    UINT32 Row;
    UINT32 Col;
    const UINT8 *Glyph;
    UINT32 Index;

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

    if (Text == 0) {
        return;
    }
    while (*Text) {
        char C = *Text++;
        if (C == '\n') {
            Cursor = X;
            Y += FONT_CELL_H;
            continue;
        }
        HalVideoDrawCharAt(Cursor, Y, C, Color);
        Cursor += FONT_CELL_W;
    }
}

void HalVideoDrawChar(char C, UINT32 Color) {
    HalVideoDrawCharAt(0, 0, C, Color);
}

void HalVideoDrawString(const char *Text, UINT32 Color) {
    HalVideoDrawStringAt(0, 0, Text, Color);
}
