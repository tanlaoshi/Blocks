/*
 * BootInfo.c — 全局开机说明书仓储（Core）
 *
 * 【初学者】
 * - Handoff 译好的 BOOT_INFO 拷贝进 gBootInfo；模块只读 BootInfoGet()。
 * - 入口：BootInfoSave（KernelMain）；BootInfoGet；BootInfoToVideoConfig。
 * - 边界：不解析 UEFI/DTB；翻译在各 Hal/KernelHandoff.c。
 */
#include "BootInfo.h"

static BOOT_INFO gBootInfo;
static int gBootInfoValid;

/*
 * BootInfoSave — 拷贝 Handoff 给出的 BOOT_INFO
 *
 * 做什么：整结构复制到 gBootInfo；Info==0 则标记无效。
 * 谁调用：KernelMain（Handoff 之后第一行）。
 * 前后文：后 — HalCapabilityObserveFrameBuffer、各模块 BootInfoGet。
 */
void BootInfoSave(const BOOT_INFO *Info) {
    UINT8 *Dst;
    const UINT8 *Src;
    UINTN i;

    if (!Info) {
        gBootInfoValid = 0;
        return;
    }
    Dst = (UINT8 *)&gBootInfo;
    Src = (const UINT8 *)Info;
    for (i = 0; i < sizeof(gBootInfo); i++) {
        Dst[i] = Src[i];
    }
    gBootInfoValid = 1;
}

/*
 * BootInfoGet — 只读开机说明书
 *
 * 做什么：返回 gBootInfo 指针；未 Save 则 NULL。
 * 谁调用：PhysicalMemoryInitialize、KernelAttachEarly、Hal 等。
 */
const BOOT_INFO *BootInfoGet(void) {
    if (!gBootInfoValid) {
        return 0;
    }
    return &gBootInfo;
}

/*
 * BootInfoToVideoConfig — BOOT_INFO → HalVideo 用的 VIDEO_CONFIG
 *
 * 做什么：字段一一映射帧缓冲宽高与基址。
 * 谁调用：KernelAttachEarly。
 */
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
