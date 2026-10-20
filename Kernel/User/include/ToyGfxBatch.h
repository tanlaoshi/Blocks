/*
 * GfxBatch.h — 用户态绘制命令缓冲（PR-GFX-batch-0）
 *
 * 一帧内收集绘制命令，GfxBatchEnd 统一回放至既有 ToyGfx*（64×64 分块）。
 * 不改 ToyGfx.h ABI；新增本头 + 库。
 *
 * 用法：
 *   GfxBatchBegin(Wid);
 *   GfxBatchFillRect(...);
 *   GfxBatchDrawRect(...);
 *   GfxBatchDrawLine(...);
 *   GfxBatchDrawPixel(...);
 *   GfxBatchText("...");
 *   GfxBatchEnd();   -- 统一回放
 */
#ifndef GFX_BATCH_H
#define GFX_BATCH_H

#define GFX_BATCH_CMD_MAX 64

typedef enum {
    GFX_BATCH_NONE = 0,
    GFX_BATCH_FILL,
    GFX_BATCH_RECT,
    GFX_BATCH_LINE,
    GFX_BATCH_PIXEL,
    GFX_BATCH_TEXT
} GFX_BATCH_KIND;

typedef struct {
    int Kind;
    int X0, Y0, X1, Y1;
    unsigned Color;
    char Text[20];
} GFX_BATCH_CMD;

int GfxBatchBegin(int WindowId);
int GfxBatchFillRect(unsigned X, unsigned Y, unsigned W, unsigned H,
                         unsigned Color);
int GfxBatchDrawRect(unsigned X, unsigned Y, unsigned W, unsigned H,
                        unsigned Color);
int GfxBatchDrawLine(int X0, int Y0, int X1, int Y1, unsigned Color);
int GfxBatchDrawPixel(unsigned X, unsigned Y, unsigned Color);
int GfxBatchText(const char *Text);
int GfxBatchEnd(void);

#endif
