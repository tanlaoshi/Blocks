/*
 * Gfx.c — 几何。未注册画点时落笔为空，不写内存。
 */
#include "Gfx.h"

static void (*gDrawPixel)(UINT32 X, UINT32 Y, UINT32 Color);
static void (*gFillRect)(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color);

void GfxSetPixel(void (*DrawPixel)(UINT32 X, UINT32 Y, UINT32 Color)) {
    gDrawPixel = DrawPixel;
}

void GfxSetFillRect(void (*FillRect)(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color)) {
    gFillRect = FillRect;
}

static void Plot(UINT32 X, UINT32 Y, UINT32 Color) {
    if (gDrawPixel) {
        gDrawPixel(X, Y, Color);
    }
}

static int Abs(int X) {
    return (X < 0) ? -X : X;
}

static int ISqrt(int N) {
    int X;
    int Y;

    if (N <= 0) {
        return 0;
    }
    X = N;
    Y = (X + 1) / 2;
    while (Y < X) {
        X = Y;
        Y = (X + N / X) / 2;
    }
    return X;
}

void GfxDrawLine(UINT32 X1, UINT32 Y1, UINT32 X2, UINT32 Y2, UINT32 Color) {
    int X0 = (int)X1;
    int Y0 = (int)Y1;
    int Xn = (int)X2;
    int Yn = (int)Y2;
    int Dx = Abs(Xn - X0);
    int Sx = X0 < Xn ? 1 : -1;
    int Dy = -Abs(Yn - Y0);
    int Sy = Y0 < Yn ? 1 : -1;
    int Err = Dx + Dy;

    for (;;) {
        Plot((UINT32)X0, (UINT32)Y0, Color);
        if (X0 == Xn && Y0 == Yn) {
            break;
        }
        {
            int E2 = 2 * Err;
            if (E2 >= Dy) {
                Err += Dy;
                X0 += Sx;
            }
            if (E2 <= Dx) {
                Err += Dx;
                Y0 += Sy;
            }
        }
    }
}

void GfxDrawRectangle(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color) {
    if (Width < 2 || Height < 2) {
        return;
    }
    GfxDrawLine(X, Y, X + Width - 1, Y, Color);
    GfxDrawLine(X, Y + Height - 1, X + Width - 1, Y + Height - 1, Color);
    GfxDrawLine(X, Y, X, Y + Height - 1, Color);
    GfxDrawLine(X + Width - 1, Y, X + Width - 1, Y + Height - 1, Color);
}

void GfxFillRectangle(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Color) {
    UINT32 Row;
    UINT32 Col;

    if (gFillRect) {
        gFillRect(X, Y, Width, Height, Color);
        return;
    }
    for (Row = 0; Row < Height; Row++) {
        for (Col = 0; Col < Width; Col++) {
            Plot(X + Col, Y + Row, Color);
        }
    }
}

void GfxDrawRoundRectangle(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Radius, UINT32 Color) {
    if (Radius > Width / 2) {
        Radius = Width / 2;
    }
    if (Radius > Height / 2) {
        Radius = Height / 2;
    }
    if (Radius == 0) {
        GfxDrawRectangle(X, Y, Width, Height, Color);
        return;
    }

    GfxDrawLine(X + Radius, Y, X + Width - Radius, Y, Color);
    GfxDrawLine(X + Radius, Y + Height, X + Width - Radius, Y + Height, Color);
    GfxDrawLine(X, Y + Radius, X, Y + Height - Radius, Color);
    GfxDrawLine(X + Width, Y + Radius, X + Width, Y + Height - Radius, Color);

    {
        int R = (int)Radius;
        int Cx1 = (int)(X + Radius);
        int Cy1 = (int)(Y + Radius);
        int Cx2 = (int)(X + Width - Radius);
        int Cy2 = (int)(Y + Radius);
        int Cx3 = (int)(X + Radius);
        int Cy3 = (int)(Y + Height - Radius);
        int Cx4 = (int)(X + Width - Radius);
        int Cy4 = (int)(Y + Height - Radius);
        int PX = 0;
        int PY = R;
        int D = 3 - 2 * R;

        while (PX <= PY) {
            Plot((UINT32)(Cx1 - PX), (UINT32)(Cy1 - PY), Color);
            Plot((UINT32)(Cx1 - PY), (UINT32)(Cy1 - PX), Color);
            Plot((UINT32)(Cx2 + PX), (UINT32)(Cy2 - PY), Color);
            Plot((UINT32)(Cx2 + PY), (UINT32)(Cy2 - PX), Color);
            Plot((UINT32)(Cx3 - PX), (UINT32)(Cy3 + PY), Color);
            Plot((UINT32)(Cx3 - PY), (UINT32)(Cy3 + PX), Color);
            Plot((UINT32)(Cx4 + PX), (UINT32)(Cy4 + PY), Color);
            Plot((UINT32)(Cx4 + PY), (UINT32)(Cy4 + PX), Color);
            if (D < 0) {
                D += 4 * PX + 6;
            } else {
                D += 4 * (PX - PY) + 10;
                PY--;
            }
            PX++;
        }
    }
}

void GfxFillRoundRectangle(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, UINT32 Radius, UINT32 Color) {
    if (Radius > Width / 2) {
        Radius = Width / 2;
    }
    if (Radius > Height / 2) {
        Radius = Height / 2;
    }
    if (Radius == 0) {
        GfxFillRectangle(X, Y, Width, Height, Color);
        return;
    }

    for (UINT32 Row = 0; Row < Height; Row++) {
        for (UINT32 Col = 0; Col < Width; Col++) {
            int DeltaLeft = (int)Col - (int)Radius;
            int DeltaRight = (int)(Width - 1 - Col) - (int)Radius;
            int DeltaTop = (int)Row - (int)Radius;
            int DeltaBottom = (int)(Height - 1 - Row) - (int)Radius;
            int Inside = 1;

            if (Col < Radius && Row < Radius) {
                if (DeltaLeft * DeltaLeft + DeltaTop * DeltaTop > (int)(Radius * Radius)) {
                    Inside = 0;
                }
            }
            if (Col > Width - 1 - Radius && Row < Radius) {
                if (DeltaRight * DeltaRight + DeltaTop * DeltaTop > (int)(Radius * Radius)) {
                    Inside = 0;
                }
            }
            if (Col < Radius && Row > Height - 1 - Radius) {
                if (DeltaLeft * DeltaLeft + DeltaBottom * DeltaBottom > (int)(Radius * Radius)) {
                    Inside = 0;
                }
            }
            if (Col > Width - 1 - Radius && Row > Height - 1 - Radius) {
                if (DeltaRight * DeltaRight + DeltaBottom * DeltaBottom > (int)(Radius * Radius)) {
                    Inside = 0;
                }
            }
            if (Inside) {
                Plot(X + Col, Y + Row, Color);
            }
        }
    }
}

void GfxDrawCircle(UINT32 CenterX, UINT32 CenterY, UINT32 Radius, UINT32 Color) {
    if (Radius == 0) {
        Plot(CenterX, CenterY, Color);
        return;
    }
    {
        int PX = 0;
        int PY = (int)Radius;
        int D = 3 - 2 * (int)Radius;

        while (PY >= PX) {
            Plot(CenterX + (UINT32)PX, CenterY + (UINT32)PY, Color);
            Plot(CenterX - (UINT32)PX, CenterY + (UINT32)PY, Color);
            Plot(CenterX + (UINT32)PX, CenterY - (UINT32)PY, Color);
            Plot(CenterX - (UINT32)PX, CenterY - (UINT32)PY, Color);
            Plot(CenterX + (UINT32)PY, CenterY + (UINT32)PX, Color);
            Plot(CenterX - (UINT32)PY, CenterY + (UINT32)PX, Color);
            Plot(CenterX + (UINT32)PY, CenterY - (UINT32)PX, Color);
            Plot(CenterX - (UINT32)PY, CenterY - (UINT32)PX, Color);
            if (D < 0) {
                D += 4 * PX + 6;
            } else {
                D += 4 * (PX - PY) + 10;
                PY--;
            }
            PX++;
        }
    }
}

void GfxFillCircle(UINT32 CenterX, UINT32 CenterY, UINT32 Radius, UINT32 Color) {
    int PY;

    if (Radius == 0) {
        Plot(CenterX, CenterY, Color);
        return;
    }
    for (PY = -(int)Radius; PY <= (int)Radius; PY++) {
        int PX = ISqrt((int)(Radius * Radius) - PY * PY);
        int I;
        for (I = -PX; I <= PX; I++) {
            Plot(CenterX + (UINT32)I, CenterY + (UINT32)PY, Color);
        }
    }
}

void GfxDrawTriangle(UINT32 X1, UINT32 Y1, UINT32 X2, UINT32 Y2, UINT32 X3, UINT32 Y3, UINT32 Color) {
    GfxDrawLine(X1, Y1, X2, Y2, Color);
    GfxDrawLine(X2, Y2, X3, Y3, Color);
    GfxDrawLine(X3, Y3, X1, Y1, Color);
}

void GfxFillTriangle(UINT32 X1, UINT32 Y1, UINT32 X2, UINT32 Y2, UINT32 X3, UINT32 Y3, UINT32 Color) {
    GfxDrawTriangle(X1, Y1, X2, Y2, X3, Y3, Color);
}
