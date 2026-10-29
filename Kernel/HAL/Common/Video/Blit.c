/*
 * Blit.c — 矩形拷贝（PR-S-video-1）
 */
#include "Private.h"
#include "HalDevices.h"

extern void *memcpy(void *Dst, const void *Src, UINTN Len);
extern void *memmove(void *Dst, const void *Src, UINTN Len);

void VideoCopyRect(UINT32 SrcX, UINT32 SrcY, UINT32 DstX, UINT32 DstY,
                   UINT32 Width, UINT32 Height) {
    UINT32 *Fb = DrawBase();
    UINT32 Pitch = DrawPitch();
    INT32 Y;
    INT32 W;
    INT32 H;
    UINT32 CopyW = Width;
    UINT32 CopyH = Height;
    UINTN RowBytes;
    UINT32 S;
    UINT32 DirtyX;
    UINT32 DirtyY;
    UINT32 DirtyW;
    UINT32 DirtyH;

    if (!Fb || !Width || !Height || Pitch == 0) {
        return;
    }
    if (SrcX >= gScreen.Width || SrcY >= gScreen.Height ||
        DstX >= gScreen.Width || DstY >= gScreen.Height) {
        return;
    }
    if (SrcX + CopyW > gScreen.Width) {
        CopyW = gScreen.Width - SrcX;
    }
    if (DstX + CopyW > gScreen.Width) {
        CopyW = gScreen.Width - DstX;
    }
    if (SrcY + CopyH > gScreen.Height) {
        CopyH = gScreen.Height - SrcY;
    }
    if (DstY + CopyH > gScreen.Height) {
        CopyH = gScreen.Height - DstY;
    }
    W = (INT32)CopyW;
    H = (INT32)CopyH;
    if (W <= 0 || H <= 0) {
        return;
    }

    /* 脏矩形仍记逻辑坐标；绘制/后缓冲内坐标乘 S（PR-23a） */
    DirtyX = DstX;
    DirtyY = DstY;
    DirtyW = CopyW;
    DirtyH = CopyH;
    S = DrawScale();
    if (S != 1u) {
        SrcX *= S;
        SrcY *= S;
        DstX *= S;
        DstY *= S;
        W *= (INT32)S;
        H *= (INT32)S;
        CopyW = (UINT32)W;
        CopyH = (UINT32)H;
    }

    /* igpu-4：大且不重叠的后缓冲内拷贝可走 SRC_COPY */
    if (gBackOn && Fb == gBack && HalIgpuReady()
        && ((UINT64)CopyW * (UINT64)CopyH >= (UINT64)HalIgpuBlitMinPixels())
        && HalIgpuCopyRectBack(gBack, gBackPitch, gDrawH ? gDrawH : gScreen.Height,
                            SrcX, SrcY, DstX, DstY, CopyW, CopyH)) {
        DirtyUnion(DirtyX, DirtyY, DirtyW, DirtyH);
        return;
    }

    RowBytes = (UINTN)W * sizeof(UINT32);
    /*
     * 按行 memcpy/memmove。勿对全屏做整块 memmove：4K 单次上滚约数十 MB，
     * 开机日志会极慢，且 live front 上仍可能被扫成「波浪」。
     */
    if (DstY == SrcY) {
        for (Y = 0; Y < H; Y++) {
            UINT32 *Dst = &Fb[(DstY + (UINT32)Y) * Pitch + DstX];
            UINT32 *Src = &Fb[(SrcY + (UINT32)Y) * Pitch + SrcX];
            memmove(Dst, Src, RowBytes);
        }
    } else if (DstY > SrcY) {
        for (Y = H - 1; Y >= 0; Y--) {
            UINT32 *Dst = &Fb[(DstY + (UINT32)Y) * Pitch + DstX];
            UINT32 *Src = &Fb[(SrcY + (UINT32)Y) * Pitch + SrcX];
            memcpy(Dst, Src, RowBytes);
        }
    } else {
        for (Y = 0; Y < H; Y++) {
            UINT32 *Dst = &Fb[(DstY + (UINT32)Y) * Pitch + DstX];
            UINT32 *Src = &Fb[(SrcY + (UINT32)Y) * Pitch + SrcX];
            memcpy(Dst, Src, RowBytes);
        }
    }
    DirtyUnion(DirtyX, DirtyY, DirtyW, DirtyH);
}

void VideoReadRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 *Out) {
    UINT32 *Fb = DrawBase();
    UINT32 Pitch = DrawPitch();
    UINT32 Row;
    UINT32 Col;
    UINT32 i = 0;
    UINT32 S;

    if (!Fb || !Out || !Width || !Height || Pitch == 0) {
        return;
    }
    if (X >= gScreen.Width || Y >= gScreen.Height) {
        return;
    }
    if (X + Width > gScreen.Width) {
        Width = gScreen.Width - X;
    }
    if (Y + Height > gScreen.Height) {
        Height = gScreen.Height - Y;
    }
    /* 逻辑矩形按块左上角采样；块内同色，等价于原逻辑后缓冲读出 */
    S = DrawScale();
    for (Row = 0; Row < Height; Row++) {
        for (Col = 0; Col < Width; Col++) {
            Out[i++] = FbPack(Fb[(Y + Row) * S * Pitch + (X + Col) * S]);
        }
    }
}

void VideoWriteRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, const UINT32 *In) {
    UINT32 *Fb = DrawBase();
    UINT32 Pitch = DrawPitch();
    UINT32 Row;
    UINT32 SrcStride;
    UINT32 CopyW;
    UINT32 CopyH;
    UINT32 S;

    if (!Fb || !In || !Width || !Height || Pitch == 0) {
        return;
    }
    if (X >= gScreen.Width || Y >= gScreen.Height) {
        return;
    }
    SrcStride = Width;
    CopyW = Width;
    CopyH = Height;
    if (X + CopyW > gScreen.Width) {
        CopyW = gScreen.Width - X;
    }
    if (Y + CopyH > gScreen.Height) {
        CopyH = gScreen.Height - Y;
    }
    DirtyUnion(X, Y, CopyW, CopyH);
    /* 输入仍是逻辑像素；每个写成 S×S 块（PR-23a） */
    S = DrawScale();
    for (Row = 0; Row < CopyH; Row++) {
        UINT32 Sy;

        for (Sy = 0; Sy < S; Sy++) {
            UINT32 *Dst = &Fb[((Y + Row) * S + Sy) * Pitch + X * S];

            for (UINT32 Col = 0; Col < CopyW; Col++) {
                UINT32 C = FbPack(In[Row * SrcStride + Col]);
                UINT32 Sx;

                for (Sx = 0; Sx < S; Sx++) {
                    Dst[Col * S + Sx] = C;
                }
            }
        }
    }
}
