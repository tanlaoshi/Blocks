/*
 * GraphicsDrawString.h — 用户窗客户区按坐标画字（SYS_GRAPHICS_DRAW_STRING）
 *
 * 内核与用户共用；定长文案，避免第三指针。
 */
#ifndef GRAPHICS_DRAW_STRING_H
#define GRAPHICS_DRAW_STRING_H

#include "Compiler.h"

#define GRAPHICS_DRAW_STRING_TEXT_MAX 120

typedef struct {
    unsigned X;
    unsigned Y;
    unsigned Color; /* 0x00RRGGBB */
    char Text[GRAPHICS_DRAW_STRING_TEXT_MAX];
} GRAPHICS_DRAW_STRING;

BLOCKS_STATIC_ASSERT(sizeof(GRAPHICS_DRAW_STRING) == 132,
                     "draw string abi is 132 bytes");

#endif
