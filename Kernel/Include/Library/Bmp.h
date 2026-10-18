/*
 * Bmp.h — 未压缩 BMP 解码（PR-G13 壁纸）
 *
 * 仅支持 BI_RGB 24/32 bpp、正高度或负高度（top-down）。
 * 输出 0x00RRGGBB。像素缓冲由调用者分配和释放。
 */
#ifndef BMP_H
#define BMP_H

#include "BootTypes.h"

typedef struct {
    UINT32 *Pixels; /* 行主序，上→下 */
    UINT32  Width;
    UINT32  Height;
    UINT32  Pages;  /* 调用者分配的页数 */
} BMP_IMAGE;

/* 量出像素缓冲字节数。成功写出宽高并返回 Width*Height*4；失败返回 0。 */
UINT32 BmpPixelBytes(const void *Data, UINTN Size, UINT32 *Width, UINT32 *Height);

/* 写入调用者提供的像素缓冲。成功 0。缓冲至少 Width*Height 个像素。 */
int BmpDecode(const void *Data, UINTN Size, UINT32 *Pixels);

#endif
