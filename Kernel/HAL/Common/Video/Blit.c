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

/*
 * PR-23c：物理像素矩形读写。
 *
 * 坐标/尺寸已是后缓冲像素（调用方用 逻辑×DrawScale 算好），这里绝不再乘
 * DrawScale，也不再按 2×2 块采样——因此「读回一块、之后原样写回」能逐像素
 * 还原，200% 下不会再把手写笔迹/字形抹成块。
 * 行距仍是 DrawPitch（无后缓冲时即 gFrontPitch）；Out/In 的行距按传入的
 * Wp 紧排，越界裁剪只缩小填充范围、不改 stride（便于 Read→Write 成对使用）。
 * 越界裁剪到 gDrawW/gDrawH；脏区仍记逻辑坐标，Present 再乘回 S。
 */
static UINT32 PhysDrawW(void) {
    return gDrawW ? gDrawW : gScreen.Width * DrawScale();
}

static UINT32 PhysDrawH(void) {
    return gDrawH ? gDrawH : gScreen.Height * DrawScale();
}

static int PhysRectClip(UINT32 *Xp, UINT32 *Yp, UINT32 *Wp, UINT32 *Hp,
                        UINT32 *Lx, UINT32 *Ly, UINT32 *Lw, UINT32 *Lh) {
    UINT32 S = DrawScale();
    UINT32 W = PhysDrawW();
    UINT32 H = PhysDrawH();
    UINT32 X1;
    UINT32 Y1;

    if (*Wp == 0 || *Hp == 0 || W == 0 || H == 0) {
        return 0;
    }
    if (*Xp >= W || *Yp >= H) {
        return 0;
    }
    if (*Wp > W - *Xp) {
        *Wp = W - *Xp;
    }
    if (*Hp > H - *Yp) {
        *Hp = H - *Yp;
    }
    /* 物理矩形覆盖的逻辑矩形（向上取整：半个块也要整块重绘） */
    X1 = *Xp + *Wp;
    Y1 = *Yp + *Hp;
    *Lx = *Xp / S;
    *Ly = *Yp / S;
    *Lw = ((X1 + S - 1u) / S) - *Lx;
    *Lh = ((Y1 + S - 1u) / S) - *Ly;
    return 1;
}

void VideoReadRectPhys(UINT32 Xp, UINT32 Yp, UINT32 Wp, UINT32 Hp, UINT32 *Out) {
    UINT32 *Fb;
    UINT32 Pitch;
    UINT32 Stride = Wp;
    UINT32 Row;
    UINT32 Col;
    UINT32 Lx;
    UINT32 Ly;
    UINT32 Lw;
    UINT32 Lh;

    if (!Out || Wp == 0 || Hp == 0) {
        return;
    }
    Fb = DrawBase();
    Pitch = DrawPitch();
    if (!Fb || Pitch == 0) {
        return;
    }
    if (!PhysRectClip(&Xp, &Yp, &Wp, &Hp, &Lx, &Ly, &Lw, &Lh)) {
        return;
    }
    for (Row = 0; Row < Hp; Row++) {
        const UINT32 *Src = &Fb[(Yp + Row) * Pitch + Xp];
        UINT32 *Dst = &Out[Row * Stride];

        if (!gFbSwapRb) {
            memcpy(Dst, Src, (UINTN)Wp * sizeof(UINT32));
        } else {
            for (Col = 0; Col < Wp; Col++) {
                Dst[Col] = FbPack(Src[Col]);
            }
        }
    }
}

void VideoWriteRectPhys(UINT32 Xp, UINT32 Yp, UINT32 Wp, UINT32 Hp, const UINT32 *In) {
    UINT32 *Fb;
    UINT32 Pitch;
    UINT32 Stride = Wp;
    UINT32 Row;
    UINT32 Col;
    UINT32 Lx;
    UINT32 Ly;
    UINT32 Lw;
    UINT32 Lh;

    if (!In || Wp == 0 || Hp == 0) {
        return;
    }
    Fb = DrawBase();
    Pitch = DrawPitch();
    if (!Fb || Pitch == 0) {
        return;
    }
    if (!PhysRectClip(&Xp, &Yp, &Wp, &Hp, &Lx, &Ly, &Lw, &Lh)) {
        return;
    }
    DirtyUnion(Lx, Ly, Lw, Lh);
    for (Row = 0; Row < Hp; Row++) {
        UINT32 *Dst = &Fb[(Yp + Row) * Pitch + Xp];
        const UINT32 *Src = &In[Row * Stride];

        if (!gFbSwapRb) {
            memcpy(Dst, Src, (UINTN)Wp * sizeof(UINT32));
        } else {
            for (Col = 0; Col < Wp; Col++) {
                Dst[Col] = FbPack(Src[Col]);
            }
        }
    }
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
