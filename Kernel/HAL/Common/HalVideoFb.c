/*
 * HalVideoFb.c — 非 x86 HalVideo：FB Device 映射与 dcache sync（对齐 X64 HalVideoFb.c）
 *
 * Present / 绘制门面见 HalVideo.c；像素/文字见 HalVideoDraw.c。
 * 从 HalVideo.c 原样搬出（纯搬家），行为零变化。
 */
#include "HalVideo.h"
#include "Video/Video.h"
#include "Hal.h"
#include "HalPat.h"
#include "HalSerial.h"
#include "BoardConfig.h"

/* PR-G-fb-pte：x86 才有完整 PTE 行；非 x86 空 */
void HalVideoLogFbPte(void) {
}

int HalVideoFbPteLine(char *Buf, UINTN Max) {
    if (Buf && Max > 0) {
        Buf[0] = 0;
    }
    return 0;
}

/*
 * rpi-9/boot-flick：真机 FB 映 Device（HAL_PAGE_PWT → Attr1），对齐 X64 FB-WC。
 * virt/ramfb 同缓存域，保持 Normal WB。
 *
 * 早映：VMM MapIdentity(FB) 即用 FbMapFlags（Attr1），MMU 一开就是 Device。
 * EnableFbWc 只记账+BootMark——勿再全 FB civac（曾与屏上滚叠成开机「闪一下」）。
 */
static int gFbDeviceMapped;

UINT64 HalVideoFbMapFlags(void) {
#if defined(__aarch64__) && !BOARD_IS_VIRT
    return HAL_PAGE_PRESENT | HAL_PAGE_WRITABLE | HAL_PAGE_PWT;
#else
    if (gFbDeviceMapped) {
        return HAL_PAGE_PRESENT | HAL_PAGE_WRITABLE | HAL_PAGE_PWT;
    }
    return HAL_PAGE_PRESENT | HAL_PAGE_WRITABLE;
#endif
}

void HalVideoEnableFbWc(void) {
#if defined(__aarch64__) && !BOARD_IS_VIRT
    UINT64 Base = VideoFrameBufferBase();

    if (Base == 0 || VideoFrameBufferSize() == 0) {
        HalSerialBootMark("Boot: FB-WC Skip (No FB)\n");
        return;
    }
    gFbDeviceMapped = 1;
    HalSerialBootMark("Boot: FB-WC OK (MAIR Attr1 Device)\n");
#else
    (void)0;
#endif
}

/*
 * Pi VC 与 CPU 不同 cache 域：凡写 scanout（Present / front 直写）后须 clean。
 * 只 EndFront 刷、Present 不刷 → GuiRedraw 桌面困在 D-cache，屏上仍是 boot 黑字。
 *
 * PresentFlush 会连打多次 Present（默认 64 行条带）。若每条都 sync，HDMI 上会
 * 「一条条撕开」：上半深色桌面贴 U-Boot 黑底不易察觉，浅色任务栏一到就像
 * 「U-Boot 界面底部先出半个桌面」。Flush 期间推迟 sync，整屏拷完再一次 clean。
 *
 * 只 sync 可见 FB。mailbox 回报的 FrameBufferSize 可能远大于 pitch×H
 *（双缓冲尾），若 dc civac 扫进 0x3F000000 外设窗，会砸坏 mini-UART TX
 *（RX 仍能收 halt，CoolTerm 却全无日志）。
 */
void HalVideoFbDcacheSync(void) {
    UINT64 Base = VideoFrameBufferBase();
    UINT64 Size = VideoFrameBufferSize();
    UINT32 Pw = 0;
    UINT32 Ph = 0;
    UINT64 OneScreen;
    const UINT64 Peri = 0x3F000000ULL;

    if (Base == 0 || Size == 0) {
        return;
    }
    VideoGetPhysicalSize(&Pw, &Ph);
    if (Pw != 0 && Ph != 0) {
        OneScreen = (UINT64)Pw * (UINT64)Ph * 4ull;
        /* Size 常含 2× 虚高；pitch≥W 时 OneScreen 略低估，允许到 2 屏 */
        if (Size > OneScreen * 2ull) {
            Size = OneScreen;
        }
    }
    if (Base >= Peri) {
        return;
    }
    if (Base + Size > Peri) {
        Size = Peri - Base;
    }
    if (Size != 0) {
        HalDcacheCleanInvalidateRange((void *)(UINTN)Base, (UINTN)Size);
    }
}
