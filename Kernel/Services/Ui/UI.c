/*
 * UI.c — 几何薄包装。算法在 Library/Gfx；画点在 InitializeVideo 里登记。
 * 半透明仍走 HalVideo。登记之前调用几何函数是空操作。
 */
#include "UI.h"
#include "Gfx.h"
#include "HalVideo.h"

void UiDrawLine(UINT32 X1, UINT32 Y1, UINT32 X2, UINT32 Y2, UINT32 Color) {
    GfxDrawLine(X1, Y1, X2, Y2, Color);
}

void UiDrawRectangle(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color) {
    GfxDrawRectangle(X, Y, Width, Height, Color);
}

void UiFillRectangle(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color) {
    GfxFillRectangle(X, Y, Width, Height, Color);
}

/* 半透明留在 Services，不进 Library/Gfx。 */
UINT32 UiBlendRgb(UINT32 Dst, UINT32 Src, UINT8 Alpha) {
    return HalVideoBlendRgb(Dst, Src, Alpha);
}

void UiFillRectangleAlpha(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height,
                          UINT32 Color, UINT8 Alpha) {
    HalVideoBlendFillRect(X, Y, Width, Height, Color, Alpha);
}

void UiDrawRoundRectangle(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Radius, UINT32 Color) {
    GfxDrawRoundRectangle(X, Y, Width, Height, Radius, Color);
}

void UiFillRoundRectangle(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Radius, UINT32 Color) {
    GfxFillRoundRectangle(X, Y, Width, Height, Radius, Color);
}

void UiDrawCircle(UINT32 CenterX, UINT32 CenterY, UINT32 Radius, UINT32 Color) {
    GfxDrawCircle(CenterX, CenterY, Radius, Color);
}

void UiFillCircle(UINT32 CenterX, UINT32 CenterY, UINT32 Radius, UINT32 Color) {
    GfxFillCircle(CenterX, CenterY, Radius, Color);
}

void UiDrawTriangle(UINT32 X1, UINT32 Y1, UINT32 X2, UINT32 Y2, UINT32 X3, UINT32 Y3, UINT32 Color) {
    GfxDrawTriangle(X1, Y1, X2, Y2, X3, Y3, Color);
}

void UiFillTriangle(UINT32 X1, UINT32 Y1, UINT32 X2, UINT32 Y2, UINT32 X3, UINT32 Y3, UINT32 Color) {
    GfxFillTriangle(X1, Y1, X2, Y2, X3, Y3, Color);
}
