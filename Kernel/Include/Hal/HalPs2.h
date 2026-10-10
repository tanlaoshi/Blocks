/*
 * HalPs2.h — i8042 统一排空（对标现网 InputPs2 Ps2Poll）
 *
 * 【初学者】
 * 读 status → 立刻读 data → 按 AUX 位分流。禁止键/鼠各读各的抢 OBF。
 * Present 等长路径里也要调，否则 1 字节缓冲溢包失步。
 */
#ifndef HAL_PS2_H
#define HAL_PS2_H

#include "BootTypes.h"

void HalPs2Poll(void);
/* 由 HalPs2Poll 调用；把一字节键盘扫描码喂进译码/队列 */
void HalPs2KeyboardFeed(UINT8 Byte);

#endif
