/*
 * BootInfo.c — 全局 BOOT_INFO 快照（KernelHandoff 写入，其后只读）
 *
 * 本刀只迁到 KernelMain 门口；完整模块表未迁入。
 */
#include "BootInfo.h"

static BOOT_INFO gBootInfo;
static int gBootInfoValid;

void BootInfoSet(const BOOT_INFO *Info) {
    UINT8 *Dst;
    const UINT8 *Src;
    UINTN i;

    if (!Info) {
        gBootInfoValid = 0;
        return;
    }
    /* 不用 memcpy：裸机 freestanding 未必链 libc */
    Dst = (UINT8 *)&gBootInfo;
    Src = (const UINT8 *)Info;
    for (i = 0; i < sizeof(gBootInfo); i++) {
        Dst[i] = Src[i];
    }
    gBootInfoValid = 1;
}

const BOOT_INFO *BootInfoGet(void) {
    if (!gBootInfoValid) {
        return 0;
    }
    return &gBootInfo;
}

VIDEO_CONFIG BootInfoToVideoConfig(const BOOT_INFO *Info) {
    VIDEO_CONFIG V;

    if (!Info) {
        V.FrameBufferBase = 0;
        V.FrameBufferSize = 0;
        V.HorizontalResolution = 0;
        V.VerticalResolution = 0;
        V.PixelsPerScanLine = 0;
        return V;
    }
    V.FrameBufferBase = Info->FrameBufferBase;
    V.FrameBufferSize = Info->FrameBufferSize;
    V.HorizontalResolution = Info->HorizontalResolution;
    V.VerticalResolution = Info->VerticalResolution;
    V.PixelsPerScanLine = Info->PixelsPerScanLine;
    return V;
}
