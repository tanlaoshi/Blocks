/*
 * VideoAdvanceStack.c — 画字步进栈：退格擦宽与 CodepointAdvance 对齐
 *
 * TTF 拉丁步进常 > FontAdvanceX；固定格宽擦会留墨（Shell 2026-11-04）。
 */
#include "Private.h"

#define VIDEO_ADVANCE_STACK_MAX 128u

static UINT32 gAdvanceStack[VIDEO_ADVANCE_STACK_MAX];
static UINT32 gAdvanceDepth;

void VideoAdvanceStackClear(void) {
    gAdvanceDepth = 0;
}

void VideoAdvanceStackPush(UINT32 Advance) {
    if (Advance == 0u || gAdvanceDepth >= VIDEO_ADVANCE_STACK_MAX) {
        return;
    }
    gAdvanceStack[gAdvanceDepth++] = Advance;
}

UINT32 VideoAdvanceStackPopOr(UINT32 Fallback) {
    if (gAdvanceDepth == 0u) {
        return Fallback;
    }
    gAdvanceDepth--;
    return gAdvanceStack[gAdvanceDepth];
}
