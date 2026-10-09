/*
 * HalVideo.c — X64：LFB + 可选背缓冲（K6）
 *
 * 【初学者】
 * Set 之后可直写 GOP 帧缓冲。InitializeBackbuffer 从 PMM 要一页池，
 * 之后 Draw* 写背缓冲；Present 整屏拷回 LFB。不强制 Clear（接 Boot 画面）。
 */
#include "HalVideo.h"
#include "HalDma.h"
#include "HalPs2.h"

#define PAGE_SIZE 4096u

static VIDEO_CONFIG gVideo;
static int gVideoValid;
static UINT32 *gBack;
static int gBackOn;

static UINT32 Pitch(void) {
    if (gVideo.PixelsPerScanLine != 0) {
        return gVideo.PixelsPerScanLine;
    }
    return gVideo.HorizontalResolution;
}

static UINT32 *Front(void) {
    if (!gVideoValid || gVideo.FrameBufferBase == 0) {
        return 0;
    }
    return (UINT32 *)(UINTN)gVideo.FrameBufferBase;
}

static UINT32 *DrawTarget(void) {
    if (gBackOn && gBack != 0) {
        return gBack;
    }
    return Front();
}

void HalVideoSet(const VIDEO_CONFIG *Config) {
    if (Config == 0) {
        gVideoValid = 0;
        gBackOn = 0;
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

int HalVideoBackbufferEnabled(void) {
    return gBackOn;
}

void HalVideoInitializeBackbuffer(void) {
    UINT32 P;
    UINT64 Bytes;
    UINT32 Pages;
    UINT32 *Src;
    UINT32 *Dst;
    UINT64 i;
    UINT64 Count;

    if (!gVideoValid || gBackOn) {
        return;
    }
    P = Pitch();
    if (P == 0 || gVideo.VerticalResolution == 0) {
        return;
    }
    Bytes = (UINT64)P * (UINT64)gVideo.VerticalResolution * 4ull;
    Pages = (UINT32)((Bytes + PAGE_SIZE - 1u) / PAGE_SIZE);
    if (Pages == 0) {
        return;
    }
    gBack = (UINT32 *)HalDmaAllocatePages(Pages);
    if (gBack == 0) {
        return;
    }
    /* 接 Boot：把当前 LFB 拷进背缓冲，避免 Present 刷成未初始化花屏 */
    Src = Front();
    Dst = gBack;
    Count = Bytes / 4ull;
    if (Src != 0) {
        for (i = 0; i < Count; i++) {
            Dst[i] = Src[i];
        }
    }
    gBackOn = 1;
}

void HalVideoPresent(void) {
    UINT32 *Src;
    UINT32 *Dst;
    UINT32 P;
    UINT32 Y;
    UINT32 X;
    UINT32 W;

    if (!gBackOn || gBack == 0) {
        return;
    }
    Src = gBack;
    Dst = Front();
    if (Dst == 0) {
        return;
    }
    P = Pitch();
    W = gVideo.HorizontalResolution;
    for (Y = 0; Y < gVideo.VerticalResolution; Y++) {
        for (X = 0; X < W; X++) {
            Dst[Y * P + X] = Src[Y * P + X];
        }
    }
}

void HalVideoPresentFlush(void) {
    HalVideoPresent();
}

void HalVideoPresentRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height) {
    UINT32 *Src;
    UINT32 *Dst;
    UINT32 P;
    UINT32 Row;
    UINT32 X1;
    UINT32 Y1;

    if (!gBackOn || gBack == 0 || Width == 0 || Height == 0) {
        return;
    }
    Src = gBack;
    Dst = Front();
    if (Dst == 0) {
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
    P = Pitch();
    for (Row = Y; Row < Y1; Row++) {
        UINT32 *S = Src + Row * P + X;
        UINT32 *D = Dst + Row * P + X;
        UINT32 N = X1 - X;
        /* 按行拷：拖窗比逐像素紧循环跟手 */
        __builtin_memcpy(D, S, (UINTN)N * sizeof(UINT32));
        /* 拖窗 Present 中排空 i8042，减轻停鼠半包失步 */
        if (((Row - Y) & 15u) == 15u) {
            HalPs2Poll();
        }
    }
}

void HalVideoDrawPixel(UINT32 X, UINT32 Y, UINT32 Color) {
    UINT32 *Fb = DrawTarget();
    UINT32 P = Pitch();

    if (Fb == 0 || X >= gVideo.HorizontalResolution ||
        Y >= gVideo.VerticalResolution) {
        return;
    }
    Fb[Y * P + X] = Color;
}

UINT32 HalVideoReadPixel(UINT32 X, UINT32 Y) {
    UINT32 *Fb = DrawTarget();
    UINT32 P = Pitch();

    if (Fb == 0 || X >= gVideo.HorizontalResolution ||
        Y >= gVideo.VerticalResolution) {
        return 0;
    }
    return Fb[Y * P + X];
}

/*
 * 光标只 XOR 前缓冲（可见面）。双写背缓冲既慢，也易与 PresentRect 打架。
 * 调用方在 Present 后需自己重画光标。
 */
void HalVideoXorPixelRaw(UINT32 X, UINT32 Y, UINT32 Mask) {
    UINT32 P = Pitch();
    UINT32 *Fr;

    if (X >= gVideo.HorizontalResolution || Y >= gVideo.VerticalResolution) {
        return;
    }
    Fr = Front();
    if (Fr != 0) {
        Fr[Y * P + X] ^= Mask;
    }
}

UINT32 HalVideoFrontReadPixel(UINT32 X, UINT32 Y) {
    UINT32 *Fr = Front();
    UINT32 P = Pitch();

    if (Fr == 0 || X >= gVideo.HorizontalResolution ||
        Y >= gVideo.VerticalResolution) {
        return 0;
    }
    return Fr[Y * P + X];
}

void HalVideoFrontDrawPixel(UINT32 X, UINT32 Y, UINT32 Color) {
    UINT32 *Fr = Front();
    UINT32 P = Pitch();

    if (Fr == 0 || X >= gVideo.HorizontalResolution ||
        Y >= gVideo.VerticalResolution) {
        return;
    }
    Fr[Y * P + X] = Color;
}

void HalVideoFillRect(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height,
                      UINT32 Color) {
    UINT32 *Fb = DrawTarget();
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

UINT64 HalVideoBackbufferBase(void) {
    return gBackOn ? (UINT64)(UINTN)gBack : 0;
}
