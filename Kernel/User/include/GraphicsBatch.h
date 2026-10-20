/*
 * GraphicsBatch.h — 用户态绘制命令缓冲（PR-GFX-batch-0）
 *
 * 一帧内收集绘制命令，GraphicsBatchEnd 统一回放至既有 Graphics*（64×64 分块）。
 * 不改 Graphics.h ABI；新增本头 + 库。
 *
 * 用法：
 *   GraphicsBatchBegin(Wid);
 *   GraphicsBatchFillRect(...);
 *   GraphicsBatchDrawRect(...);
 *   GraphicsBatchDrawLine(...);
 *   GraphicsBatchDrawPixel(...);
 *   GraphicsBatchText("...");
 *   GraphicsBatchEnd();   -- 统一回放
 */
#ifndef GRAPHICS_BATCH_H
#define GRAPHICS_BATCH_H

#define GRAPHICS_BATCH_CMD_MAX 64

typedef enum {
    GRAPHICS_BATCH_NONE = 0,
    GRAPHICS_BATCH_FILL,
    GRAPHICS_BATCH_RECT,
    GRAPHICS_BATCH_LINE,
    GRAPHICS_BATCH_PIXEL,
    GRAPHICS_BATCH_TEXT
} GRAPHICS_BATCH_KIND;

typedef struct {
    int Kind;
    int X0, Y0, X1, Y1;
    unsigned Color;
    char Text[20];
} GRAPHICS_BATCH_CMD;

int GraphicsBatchBegin(int WindowId);
int GraphicsBatchFillRect(unsigned X, unsigned Y, unsigned W, unsigned H,
                         unsigned Color);
int GraphicsBatchDrawRect(unsigned X, unsigned Y, unsigned W, unsigned H,
                        unsigned Color);
int GraphicsBatchDrawLine(int X0, int Y0, int X1, int Y1, unsigned Color);
int GraphicsBatchDrawPixel(unsigned X, unsigned Y, unsigned Color);
int GraphicsBatchText(const char *Text);
int GraphicsBatchEnd(void);

#endif
