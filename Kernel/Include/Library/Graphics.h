/*
 * Graphics.h — 线、矩形、圆、圆角、三角。只通过注册的画点落笔。
 *
 * 半透明不在本库，调用 HalVideoBlendFillRect / HalVideoBlendRgb。
 */
#ifndef GRAPHICS_H
#define GRAPHICS_H

#include "BootTypes.h"

void GraphicsSetPixel(void (*DrawPixel)(UINT32 X, UINT32 Y, UINT32 Color));
/* 不注册则实心矩形用画点逐点填。 */
void GraphicsSetFillRect(void (*FillRect)(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color));

void GraphicsDrawLine(UINT32 X1, UINT32 Y1, UINT32 X2, UINT32 Y2, UINT32 Color);
void GraphicsDrawRectangle(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color);
void GraphicsFillRectangle(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color);
void GraphicsDrawRoundRectangle(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Radius, UINT32 Color);
void GraphicsFillRoundRectangle(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Radius, UINT32 Color);
void GraphicsDrawCircle(UINT32 CenterX, UINT32 CenterY, UINT32 Radius, UINT32 Color);
void GraphicsFillCircle(UINT32 CenterX, UINT32 CenterY, UINT32 Radius, UINT32 Color);
void GraphicsDrawTriangle(UINT32 X1, UINT32 Y1, UINT32 X2, UINT32 Y2, UINT32 X3, UINT32 Y3, UINT32 Color);
void GraphicsFillTriangle(UINT32 X1, UINT32 Y1, UINT32 X2, UINT32 Y2, UINT32 X3, UINT32 Y3, UINT32 Color);

#endif
