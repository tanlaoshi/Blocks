/*
 * GraphicsDamage.h — 用户贴像素矩形（SYS_DAMAGE_RECT）
 *
 * 内核和用户共用这一份。x86_64 / Arm64 / RiscV 上都是 24 字节。
 */
#ifndef GRAPHICS_DAMAGE_H
#define GRAPHICS_DAMAGE_H

#include "Compiler.h"

typedef struct {
    unsigned X;
    unsigned Y;
    unsigned W;
    unsigned H;
    const unsigned *Pixels; /* W*H × 0x00RRGGBB，行优先 */
} GRAPHICS_DAMAGE_RECT;

BLOCKS_STATIC_ASSERT(sizeof(GRAPHICS_DAMAGE_RECT) == 24, "damage rect abi is 24 bytes");

#endif
