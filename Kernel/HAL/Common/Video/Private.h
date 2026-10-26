/*
 * Private.h — Video 绘制内部共享头（仅 HAL/Common/Video/）
 *
 * 禁止 Common / User 包含；对外用 Video.h / HalVideo.h。
 */
#ifndef VIDEO_PRIVATE_H
#define VIDEO_PRIVATE_H

#include "Video.h"
#include "Font.h"
#include "PhysicalMemory.h"
#include "Hal.h"

#define PRESENT_CHUNK_ROWS 64u

/* 0=默认 PRESENT_CHUNK_ROWS；拖窗可升到极大值整块 blit，减左右条带频闪 */
extern UINT32 gPresentChunkRows;

/* 全局定义在 Video.c */
extern SCREEN_INFO gScreen;
extern UINT32 gBackground;
extern int gClipOn;
extern UINT32 gClipX;
extern UINT32 gClipY;
extern UINT32 gClipW;
extern UINT32 gClipH;
extern UINT32 gClipBg;

extern UINT32 *gFront;
extern UINT32  gFrontPitch;
extern UINT32 *gBack;
extern UINT32  gBackPitch;
extern UINT32  gBackPages;
extern int     gBackOn;
extern int     gForceFront;

extern UINT32  gPhysW;
extern UINT32  gPhysH;
extern UINT32  gUiScale;

extern int     gDirty;
extern UINT32  gDx0;
extern UINT32  gDy0;
extern UINT32  gDx1;
extern UINT32  gDy1;
extern int     gCurDirty;
extern UINT32  gCx0;
extern UINT32  gCy0;
extern UINT32  gCx1;
extern UINT32  gCy1;

extern int     gCursorOverlay;
/* Pi VC 等：scanout 为 RGB 字节序时，把 0x00RRGGBB 的 R/B 对调再写 FB */
extern int     gFbSwapRb;

static inline UINT32 FbPack(UINT32 C) {
    if (!gFbSwapRb) {
        return C;
    }
    return (C & 0xFF00FF00u) | ((C & 0xFFu) << 16) | ((C >> 16) & 0xFFu);
}

static inline UINT32 *DrawBase(void) {
    if (gForceFront && gFront) {
        return gFront;
    }
    return gBackOn ? gBack : gFront;
}

static inline UINT32 DrawPitch(void) {
    if (gForceFront && gFront) {
        return gFrontPitch;
    }
    return gBackOn ? gBackPitch : gFrontPitch;
}

static inline UINT32 CodepointAdvance(UINT32 Cp) {
    return FontCodepointAdvance(Cp);
}

/* Scale / 逻辑分辨率（Scale.c） */
UINT32 NormalizeUiScale(UINT32 Percent);
void ApplyLogicalFromPhys(void);

/* Dirty / Present（Present*.c；Draw* 经 DirtyUnion 记账） */
void DirtyUnionInto(int *Dirty, UINT32 *Dx0, UINT32 *Dy0, UINT32 *Dx1,
                    UINT32 *Dy1, UINT32 X, UINT32 Y, UINT32 W, UINT32 H);
void DirtyUnion(UINT32 X, UINT32 Y, UINT32 W, UINT32 H);
void DirtyUnionCursor(UINT32 X, UINT32 Y, UINT32 W, UINT32 H);
void PresentRectRows(UINT32 X0, UINT32 Y0, UINT32 X1, UINT32 Y1,
                     UINT64 FbBytes, int *DirtyOut, UINT32 *Dx0,
                     UINT32 *Dy0, UINT32 *Dx1, UINT32 *Dy1,
                     int *OutPartial);

#endif
