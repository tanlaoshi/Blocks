/*
 * FontTerminus10x18.h — Terminus 10×18（ASCII 0x20..0x7E）
 */
#ifndef FONT_TERMINUS_10X18_H
#define FONT_TERMINUS_10X18_H

#include "BootTypes.h"

#define FONT_TERM10_W      10u
#define FONT_TERM10_H      18u
#define FONT_TERM10_FIRST  0x20u
#define FONT_TERM10_COUNT  95u
#define FONT_TERM10_BYTES  36u /* 18 行 × 2 字节 */

extern const UINT8 gFontTerminus10x18[FONT_TERM10_COUNT][FONT_TERM10_BYTES];

static inline const UINT8 *FontTerminus10Glyph(UINT32 Cp) {
    if (Cp < FONT_TERM10_FIRST || Cp >= FONT_TERM10_FIRST + FONT_TERM10_COUNT) {
        Cp = (UINT32)'?';
    }
    return gFontTerminus10x18[Cp - FONT_TERM10_FIRST];
}

#endif
