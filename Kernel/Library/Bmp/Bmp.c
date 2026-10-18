/*
 * Bmp.c — 未压缩 BMP 解码。只填调用者给的缓冲，不申请页。
 */
#include "Bmp.h"

typedef struct {
    UINT32 Width;
    UINT32 Height;
    UINT32 OffBits;
    UINT32 RowStride;
    UINT16 BitCount;
    int TopDown;
} BMP_INFO;

static int Parse(const void *Data, UINTN Size, BMP_INFO *Info) {
    const UINT8 *P;
    UINT16 Type;
    INT32 Width;
    INT32 Height;
    UINT16 Planes;
    UINT32 Compression;
    UINT32 H;

    if (!Data || !Info || Size < 54) {
        return -1;
    }
    P = (const UINT8 *)Data;
    Type = (UINT16)(P[0] | (P[1] << 8));
    Info->OffBits = (UINT32)(P[10] | (P[11] << 8) | (P[12] << 16) | (P[13] << 24));
    if (Type != 0x4D42 || Info->OffBits >= Size) {
        return -1;
    }

    P = (const UINT8 *)Data + 14;
    Info->Width = 0;
    if ((UINT32)(P[0] | (P[1] << 8) | (P[2] << 16) | (P[3] << 24)) < 40) {
        return -1;
    }
    Width = (INT32)(P[4] | (P[5] << 8) | (P[6] << 16) | (P[7] << 24));
    Height = (INT32)(P[8] | (P[9] << 8) | (P[10] << 16) | (P[11] << 24));
    Planes = (UINT16)(P[12] | (P[13] << 8));
    Info->BitCount = (UINT16)(P[14] | (P[15] << 8));
    Compression = (UINT32)(P[16] | (P[17] << 8) | (P[18] << 16) | (P[19] << 24));

    if (Planes != 1 || Compression != 0) {
        return -1;
    }
    if (Info->BitCount != 24 && Info->BitCount != 32) {
        return -1;
    }
    if (Width <= 0 || Width > 4096) {
        return -1;
    }
    Info->TopDown = 0;
    if (Height < 0) {
        Info->TopDown = 1;
        H = (UINT32)(-Height);
    } else {
        H = (UINT32)Height;
    }
    if (H == 0 || H > 4096) {
        return -1;
    }
    Info->Width = (UINT32)Width;
    Info->Height = H;
    Info->RowStride = ((Info->Width * (Info->BitCount / 8) + 3u) & ~3u);
    if ((UINT64)Info->OffBits + (UINT64)Info->RowStride * H > Size) {
        return -1;
    }
    return 0;
}

UINT32 BmpPixelBytes(const void *Data, UINTN Size, UINT32 *Width, UINT32 *Height) {
    BMP_INFO Info;

    if (!Width || !Height || Parse(Data, Size, &Info) != 0) {
        return 0;
    }
    *Width = Info.Width;
    *Height = Info.Height;
    return Info.Width * Info.Height * (UINT32)sizeof(UINT32);
}

int BmpDecode(const void *Data, UINTN Size, UINT32 *Pixels) {
    BMP_INFO Info;
    UINT32 Y;
    UINT32 X;

    if (!Pixels || Parse(Data, Size, &Info) != 0) {
        return -1;
    }
    for (Y = 0; Y < Info.Height; Y++) {
        UINT32 SrcY = Info.TopDown ? Y : (Info.Height - 1 - Y);
        const UINT8 *SrcRow = (const UINT8 *)Data + Info.OffBits + SrcY * Info.RowStride;

        for (X = 0; X < Info.Width; X++) {
            UINT32 B;
            UINT32 G;
            UINT32 R;

            if (Info.BitCount == 24) {
                B = SrcRow[X * 3 + 0];
                G = SrcRow[X * 3 + 1];
                R = SrcRow[X * 3 + 2];
            } else {
                B = SrcRow[X * 4 + 0];
                G = SrcRow[X * 4 + 1];
                R = SrcRow[X * 4 + 2];
            }
            Pixels[Y * Info.Width + X] = (R << 16) | (G << 8) | B;
        }
    }
    return 0;
}
