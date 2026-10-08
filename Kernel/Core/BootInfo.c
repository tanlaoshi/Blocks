/*
 * BootInfo.c — 保存一份全局的开机说明书
 *
 * 【初学者 · 读本文件前先看 Include/Core/BootInfo.h】
 *
 * 流程：
 *   1. KernelHandoff 在栈/静态区填好一份 BOOT_INFO
 *   2. 调用 KernelMain(&Info)
 *   3. KernelMain 第一件事 BootInfoSet(Info) —— 拷进本文件的 gBootInfo
 *   4. 之后任何模块用 BootInfoGet() 读，不要再抓住 Handoff 的临时指针
 *
 * 为什么要「拷贝」而不是只存指针？
 *   Handoff 里的对象寿命/位置各 Arch 不同；拷到内核 BSS 最省心。
 *
 * 为什么手写字节拷贝、不用 memcpy？
 *   freestanding 内核常常不链接 libc；自己拷最稳。
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

/*
 * 只抽出「当前显示模式」几项，给 HalVideoSet 用。
 * Info 为空时返回全 0：表示「没有屏」，后面画图 API 应安全空操作。
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
