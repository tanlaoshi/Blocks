/*
 * Gfx.h — 线、矩形、圆、圆角、三角。只通过注册的画点落笔。
 *
 * 半透明不在本库，调用 HalVideoBlendFillRect / HalVideoBlendRgb。
 */
#ifndef GFX_H
#define GFX_H

#include "BootTypes.h"

void GfxSetPixel(void (*DrawPixel)(UINT32 X, UINT32 Y, UINT32 Color));
/* 不注册则实心矩形用画点逐点填。 */
void GfxSetFillRect(void (*FillRect)(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color));

void GfxDrawLine(UINT32 X1, UINT32 Y1, UINT32 X2, UINT32 Y2, UINT32 Color);
void GfxDrawRectangle(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color);
void GfxFillRectangle(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color);
void GfxDrawRoundRectangle(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Radius, UINT32 Color);
void GfxFillRoundRectangle(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Radius, UINT32 Color);
void GfxDrawCircle(UINT32 CenterX, UINT32 CenterY, UINT32 Radius, UINT32 Color);
void GfxFillCircle(UINT32 CenterX, UINT32 CenterY, UINT32 Radius, UINT32 Color);
void GfxDrawTriangle(UINT32 X1, UINT32 Y1, UINT32 X2, UINT32 Y2, UINT32 X3, UINT32 Y3, UINT32 Color);
void GfxFillTriangle(UINT32 X1, UINT32 Y1, UINT32 X2, UINT32 Y2, UINT32 X3, UINT32 Y3, UINT32 Color);

#endif
