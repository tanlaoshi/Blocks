/*
 * Graphics.c — libGraphics
 */
#include <Graphics.h>
#include <Blocks/syscall.h>

#define GRAPHICS_TILE 64u

static unsigned s_Fill[GRAPHICS_DAMAGE_RECT_MAX_PIXELS];

static int AbsI(int V) {
    return (V < 0) ? -V : V;
}

int GraphicsInitialize(void) {
    /*
     * 系统字体由内核 FontLoadAssets 扫 GRAPHICS_SYSTEM_FONT_ROOT。
     * 应用只声明约定；私有 .fnt 加载另刀，本函数不读盘。
     */
    return 0;
}

const char *GraphicsSystemFontRoot(void) {
    return GRAPHICS_SYSTEM_FONT_ROOT;
}

int GraphicsDamageText(int WindowId, const char *Text) {
    if (WindowId < 0 || !Text) {
        return -1;
    }
    return (int)toy_damage(WindowId, Text);
}

int GraphicsDamageRect(int WindowId, const GRAPHICS_DAMAGE_RECT *Desc) {
    unsigned long long N;

    if (WindowId < 0 || !Desc || !Desc->Pixels) {
        return -1;
    }
    if (Desc->W == 0 || Desc->H == 0) {
        return -1;
    }
    N = (unsigned long long)Desc->W * (unsigned long long)Desc->H;
    if (N == 0 || N > (unsigned long long)GRAPHICS_DAMAGE_RECT_MAX_PIXELS) {
        return -1;
    }
    return (int)toy_damage_rect(WindowId, Desc);
}

int GraphicsFillRect(int WindowId, unsigned X, unsigned Y, unsigned W, unsigned H,
                   unsigned Color) {
    unsigned Ox;
    unsigned Oy;
    unsigned TileW;
    unsigned TileH;
    unsigned I;
    unsigned N;
    GRAPHICS_DAMAGE_RECT Desc;

    if (WindowId < 0 || W == 0 || H == 0) {
        return -1;
    }

    for (Oy = 0; Oy < H; Oy += GRAPHICS_TILE) {
        TileH = H - Oy;
        if (TileH > GRAPHICS_TILE) {
            TileH = GRAPHICS_TILE;
        }
        for (Ox = 0; Ox < W; Ox += GRAPHICS_TILE) {
            TileW = W - Ox;
            if (TileW > GRAPHICS_TILE) {
                TileW = GRAPHICS_TILE;
            }
            N = TileW * TileH;
            for (I = 0; I < N; I++) {
                s_Fill[I] = Color;
            }
            Desc.X = X + Ox;
            Desc.Y = Y + Oy;
            Desc.W = TileW;
            Desc.H = TileH;
            Desc.Pixels = s_Fill;
            if (GraphicsDamageRect(WindowId, &Desc) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

int GraphicsDrawPixel(int WindowId, unsigned X, unsigned Y, unsigned Color) {
    return GraphicsFillRect(WindowId, X, Y, 1, 1, Color);
}

int GraphicsDrawLine(int WindowId, int X0, int Y0, int X1, int Y1, unsigned Color) {
    int Dx;
    int Dy;
    int Sx;
    int Sy;
    int Err;
    int E2;
    int X;
    int Y;

    if (WindowId < 0 || X0 < 0 || Y0 < 0 || X1 < 0 || Y1 < 0) {
        return -1;
    }
    if (Y0 == Y1) {
        unsigned Xmin = (unsigned)((X0 < X1) ? X0 : X1);
        unsigned Len = (unsigned)AbsI(X1 - X0) + 1u;
        return GraphicsFillRect(WindowId, Xmin, (unsigned)Y0, Len, 1, Color);
    }
    if (X0 == X1) {
        unsigned Ymin = (unsigned)((Y0 < Y1) ? Y0 : Y1);
        unsigned Len = (unsigned)AbsI(Y1 - Y0) + 1u;
        return GraphicsFillRect(WindowId, (unsigned)X0, Ymin, 1, Len, Color);
    }

    Dx = AbsI(X1 - X0);
    Dy = -AbsI(Y1 - Y0);
    Sx = (X0 < X1) ? 1 : -1;
    Sy = (Y0 < Y1) ? 1 : -1;
    Err = Dx + Dy;
    X = X0;
    Y = Y0;
    for (;;) {
        if (GraphicsDrawPixel(WindowId, (unsigned)X, (unsigned)Y, Color) != 0) {
            return -1;
        }
        if (X == X1 && Y == Y1) {
            break;
        }
        E2 = Err * 2;
        if (E2 >= Dy) {
            Err += Dy;
            X += Sx;
        }
        if (E2 <= Dx) {
            Err += Dx;
            Y += Sy;
        }
    }
    return 0;
}

int GraphicsDrawRect(int WindowId, unsigned X, unsigned Y, unsigned W, unsigned H,
                   unsigned Color) {
    if (W == 0 || H == 0) {
        return -1;
    }
    if (W == 1 || H == 1) {
        return GraphicsFillRect(WindowId, X, Y, W, H, Color);
    }
    if (GraphicsFillRect(WindowId, X, Y, W, 1, Color) != 0) {
        return -1;
    }
    if (GraphicsFillRect(WindowId, X, Y + H - 1, W, 1, Color) != 0) {
        return -1;
    }
    if (H > 2) {
        if (GraphicsFillRect(WindowId, X, Y + 1, 1, H - 2, Color) != 0) {
            return -1;
        }
        if (GraphicsFillRect(WindowId, X + W - 1, Y + 1, 1, H - 2, Color) != 0) {
            return -1;
        }
    }
    return 0;
}
