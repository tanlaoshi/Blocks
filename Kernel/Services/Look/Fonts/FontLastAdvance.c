/*
 * FontLastAdvance.c — 最近一次成功栅格的像素步进（FT 与 stb 共用）
 *
 * FillSlot 读 FontFtLastAdvance()：有值用真实步进，否则拉丁退半格。
 * X64 FreeType 写；Pi/软退 stb 也必须写，否则 UMing 英文按 Side/2 排字叠在一起。
 */
#include "Font.h"

static UINT32 gLastAdvance;

UINT32 FontFtLastAdvance(void) {
    return gLastAdvance;
}

void FontFtClearLastAdvance(void) {
    gLastAdvance = 0;
}

void FontFtSetLastAdvance(UINT32 Advance) {
    gLastAdvance = Advance;
}
