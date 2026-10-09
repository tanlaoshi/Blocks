/*
 * FontTtf.h — K27：盘上 TTF 最小光栅（对标现网 PR-UI-ttf-*）
 *
 * 失败软退：FontDraw* 仍走 CJK 点阵。
 */
#ifndef FONT_TTF_H
#define FONT_TTF_H

#include "BootTypes.h"

int FontTtfLoad(void);
const UINT8 *FontTtfBlob(UINT32 *OutSize);
int FontTtfInit(void);
int FontTtfRasterCp(UINT32 Cp, UINT8 *Pix);
void FontTtfPreheatUtf8(const char *S);
const UINT8 *FontTtfCacheGet(UINT32 Cp);

#endif
