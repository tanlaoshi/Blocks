/*
 * GraphicsBatch.c — 绘制命令缓冲（PR-GFX-batch-0）
 * 收集一帧命令，End 时统一回放至既有 Graphics*（64×64 分块由 Graphics 自带）。
 * 不改 Graphics.h ABI；≤300 行。
 */
#include <GraphicsBatch.h>
#include <Graphics.h>

static GRAPHICS_BATCH_CMD s_Cmd[GRAPHICS_BATCH_CMD_MAX];
static int s_N;
static int s_Wid;
static int s_Active;

static int Push(int Kind, int X0, int Y0, int X1, int Y1, unsigned Color) {
    if (!s_Active || s_N >= GRAPHICS_BATCH_CMD_MAX) {
        return -1;
    }
    s_Cmd[s_N].Kind = Kind;
    s_Cmd[s_N].X0 = X0;
    s_Cmd[s_N].Y0 = Y0;
    s_Cmd[s_N].X1 = X1;
    s_Cmd[s_N].Y1 = Y1;
    s_Cmd[s_N].Color = Color;
    s_Cmd[s_N].Text[0] = 0;
    s_N++;
    return 0;
}

int GraphicsBatchBegin(int WindowId) {
    if (WindowId < 0) {
        return -1;
    }
    s_Wid = WindowId;
    s_N = 0;
    s_Active = 1;
    return 0;
}

int GraphicsBatchFillRect(unsigned X, unsigned Y, unsigned W, unsigned H,
                         unsigned Color) {
    return Push(GRAPHICS_BATCH_FILL, (int)X, (int)Y, (int)W, (int)H, Color);
}

int GraphicsBatchDrawRect(unsigned X, unsigned Y, unsigned W, unsigned H,
                        unsigned Color) {
    return Push(GRAPHICS_BATCH_RECT, (int)X, (int)Y, (int)W, (int)H, Color);
}

int GraphicsBatchDrawLine(int X0, int Y0, int X1, int Y1, unsigned Color) {
    return Push(GRAPHICS_BATCH_LINE, X0, Y0, X1, Y1, Color);
}

int GraphicsBatchDrawPixel(unsigned X, unsigned Y, unsigned Color) {
    return Push(GRAPHICS_BATCH_PIXEL, (int)X, (int)Y, 1, 1, Color);
}

int GraphicsBatchText(const char *Text) {
    int I;

    if (!s_Active || s_N >= GRAPHICS_BATCH_CMD_MAX || !Text) {
        return -1;
    }
    s_Cmd[s_N].Kind = GRAPHICS_BATCH_TEXT;
    s_Cmd[s_N].X0 = 0;
    s_Cmd[s_N].Y0 = 0;
    s_Cmd[s_N].X1 = 0;
    s_Cmd[s_N].Y1 = 0;
    s_Cmd[s_N].Color = 0;
    for (I = 0; I + 1 < (int)sizeof(s_Cmd[s_N].Text) && Text[I]; I++) {
        s_Cmd[s_N].Text[I] = Text[I];
    }
    s_Cmd[s_N].Text[I] = 0;
    s_N++;
    return 0;
}

static void Replay(void) {
    int I;

    for (I = 0; I < s_N; I++) {
        GRAPHICS_BATCH_CMD *C = &s_Cmd[I];

        switch (C->Kind) {
        case GRAPHICS_BATCH_FILL:
            (void)GraphicsFillRect(s_Wid, (unsigned)C->X0, (unsigned)C->Y0,
                                 (unsigned)C->X1, (unsigned)C->Y1, C->Color);
            break;
        case GRAPHICS_BATCH_RECT:
            (void)GraphicsDrawRect(s_Wid, (unsigned)C->X0, (unsigned)C->Y0,
                                (unsigned)C->X1, (unsigned)C->Y1, C->Color);
            break;
        case GRAPHICS_BATCH_LINE:
            (void)GraphicsDrawLine(s_Wid, C->X0, C->Y0, C->X1, C->Y1, C->Color);
            break;
        case GRAPHICS_BATCH_PIXEL:
            (void)GraphicsDrawPixel(s_Wid, (unsigned)C->X0, (unsigned)C->Y0,
                                  C->Color);
            break;
        case GRAPHICS_BATCH_TEXT:
            (void)GraphicsDamageText(s_Wid, C->Text);
            break;
        default:
            break;
        }
    }
}

int GraphicsBatchEnd(void) {
    if (!s_Active) {
        return -1;
    }
    Replay();
    s_N = 0;
    s_Active = 0;
    s_Wid = -1;
    return 0;
}
