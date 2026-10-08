/*
 * HalVideo.c — X64：直接写线性帧缓冲（最小子集）
 *
 * 【初学者】
 * Boot 已通过 GOP 设好模式，并把帧缓冲物理址放进 BOOT_INFO。
 * 恒等页表打开后，虚址 == 物理址（落在 4GiB 窗内时），可把
 * FrameBufferBase 当成 UINT32* 数组来写像素。
 *
 * 约定：每像素 32 位，颜色 0x00RRGGBB（与常见 UEFI BGRX 小端一致）。
 * 一行宽度用 PixelsPerScanLine（可能 ≥ 可见宽度，有 padding）。
 *
 * 画字见 Core/Font.c（点阵调 DrawPixel）。本文件不做后缓冲 / Present / 剪裁。
 */
#include "HalVideo.h"

static VIDEO_CONFIG gVideo;
static int gVideoValid;

static UINT32 *FrameBuffer(void) {
    if (!gVideoValid || gVideo.FrameBufferBase == 0 ||
        gVideo.HorizontalResolution == 0 || gVideo.VerticalResolution == 0) {
        return 0;
    }
    return (UINT32 *)(UINTN)gVideo.FrameBufferBase;
}

static UINT32 Pitch(void) {
    if (gVideo.PixelsPerScanLine != 0) {
        return gVideo.PixelsPerScanLine;
    }
    return gVideo.HorizontalResolution;
}

void HalVideoSet(const VIDEO_CONFIG *Config) {
    if (Config == 0) {
        gVideoValid = 0;
        return;
    }
    gVideo = *Config;
    gVideoValid = 1;
}

void HalVideoGetSize(UINT32 *Width, UINT32 *Height) {
    if (Width) {
        *Width = gVideoValid ? gVideo.HorizontalResolution : 0;
    }
    if (Height) {
        *Height = gVideoValid ? gVideo.VerticalResolution : 0;
    }
}

UINT64 HalVideoFrameBufferBase(void) {
    return gVideoValid ? gVideo.FrameBufferBase : 0;
}

UINT64 HalVideoFrameBufferSize(void) {
    return gVideoValid ? gVideo.FrameBufferSize : 0;
}

void HalVideoDrawPixel(UINT32 X, UINT32 Y, UINT32 Color) {
    UINT32 *Fb = FrameBuffer();
    UINT32 P = Pitch();

    if (Fb == 0 || X >= gVideo.HorizontalResolution ||
        Y >= gVideo.VerticalResolution) {
        return;
    }
    Fb[Y * P + X] = Color;
}

void HalVideoFillRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height,
                      UINT32 Color) {
    UINT32 *Fb = FrameBuffer();
    UINT32 P = Pitch();
    UINT32 Row;
    UINT32 Col;
    UINT32 X1;
    UINT32 Y1;

    if (Fb == 0 || Width == 0 || Height == 0) {
        return;
    }
    if (X >= gVideo.HorizontalResolution || Y >= gVideo.VerticalResolution) {
        return;
    }
    X1 = X + Width;
    Y1 = Y + Height;
    if (X1 > gVideo.HorizontalResolution) {
        X1 = gVideo.HorizontalResolution;
    }
    if (Y1 > gVideo.VerticalResolution) {
        Y1 = gVideo.VerticalResolution;
    }
    for (Row = Y; Row < Y1; Row++) {
        for (Col = X; Col < X1; Col++) {
            Fb[Row * P + Col] = Color;
        }
    }
}
