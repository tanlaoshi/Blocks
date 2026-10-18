/*
 * GfxDamage.h — 用户贴像素矩形（SYS_DAMAGE_RECT）
 *
 * 内核和用户共用这一份。x86_64 / Arm64 / RiscV 上都是 24 字节。
 */
#ifndef GFX_DAMAGE_H
#define GFX_DAMAGE_H

typedef struct {
    unsigned X;
    unsigned Y;
    unsigned W;
    unsigned H;
    const unsigned *Pixels; /* W*H × 0x00RRGGBB，行优先 */
} TOY_GFX_DAMAGE_RECT;

_Static_assert(sizeof(TOY_GFX_DAMAGE_RECT) == 24, "damage rect abi is 24 bytes");

#endif
