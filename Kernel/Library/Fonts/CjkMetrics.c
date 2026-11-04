/*
 * CjkMetrics.c — CJK 点阵边长/位深常量（原 cjk32.c 整表已废）
 *
 * 汉字 GUI 只走 TTF（FontGlyphCp）；此处仅供缺字框 / 度量兜底。
 */
#include "Font.h"

#define CJK_BITMAP_DIMENSION 18u
#define CJK_BITMAP_BITS_PER_PIXEL 4u

UINT32 FontCjkBitsPerPixel(void) {
    return CJK_BITMAP_BITS_PER_PIXEL;
}

UINT32 FontCjkDim(void) {
    return CJK_BITMAP_DIMENSION;
}
