/*
 * BootHandoff.c — 全局交接块（各架构 KernelHandoff 写入，Common 只读）
 */
#include "BootHandoff.h"

static BOOT_HANDOFF gBootHandoff;
static int gBootHandoffValid;

void BootHandoffSet(const BOOT_HANDOFF *Handoff) {
    if (!Handoff) {
        gBootHandoffValid = 0;
        return;
    }
    gBootHandoff = *Handoff;
    gBootHandoffValid = 1;
}

const BOOT_HANDOFF *BootHandoffGet(void) {
    if (!gBootHandoffValid) {
        return 0;
    }
    return &gBootHandoff;
}

VIDEO_CONFIG BootHandoffToVideoConfig(const BOOT_HANDOFF *Handoff) {
    VIDEO_CONFIG V;

    if (!Handoff) {
        V.FrameBufferBase = 0;
        V.FrameBufferSize = 0;
        V.HorizontalResolution = 0;
        V.VerticalResolution = 0;
        V.PixelsPerScanLine = 0;
        return V;
    }
    V.FrameBufferBase = Handoff->FrameBufferBase;
    V.FrameBufferSize = Handoff->FrameBufferSize;
    V.HorizontalResolution = Handoff->HorizontalResolution;
    V.VerticalResolution = Handoff->VerticalResolution;
    V.PixelsPerScanLine = Handoff->PixelsPerScanLine;
    return V;
}
