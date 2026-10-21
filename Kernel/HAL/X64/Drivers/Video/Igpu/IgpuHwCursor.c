/*
 * IgpuHwCursor.c — hw-cursor-api / hw-cursor-igpu
 *
 * 契约：HalVideoHwCursor* → 本文件。api 刀：一律不支持（Gui 走软件末波）。
 * igpu 刀：CUR_CTL/BASE/POS + GTT ARGB；Enable 失败保持 Supported=0。
 */
#include "Igpu.h"
#include "HalVideo.h"

int HalVideoHwCursorSupported(void) {
    /* hw-cursor-igpu 接通后再：IgpuMmioOk && 探针成功 */
    return 0;
}

int HalVideoHwCursorEnable(const UINT32 *Argb, UINT32 Width, UINT32 Height,
                           UINT32 HotX, UINT32 HotY) {
    (void)Argb;
    (void)Width;
    (void)Height;
    (void)HotX;
    (void)HotY;
    return 0;
}

void HalVideoHwCursorSetPos(UINT32 X, UINT32 Y) {
    (void)X;
    (void)Y;
}

void HalVideoHwCursorDisable(void) {
}
