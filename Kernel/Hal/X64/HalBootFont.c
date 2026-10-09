/*
 * HalBootFont.c — HAL 启动屏 8×8 画字
 *
 * 【初学者】
 * 只服务 boot 期屏上滚；用 HalVideoDrawPixel，不碰 Core/Font。
 */
#include "HalBootFont.h"
#include "HalBootFontGlyph8x8.h"
#include "HalVideo.h"

UINT32 HalBootFontCellHeight(void) {
    return FONT_CELL_H;
}

static void DrawCharAt(UINT32 X, UINT32 Y, char C, UINT32 Color) {
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

void HalBootFontDrawStringAt(UINT32 X, UINT32 Y, const char *Text, UINT32 Color) {
    UINT32 Cursor = X;

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
        DrawCharAt(Cursor, Y, Ch, Color);
        Cursor += FONT_CELL_W;
    }
}
