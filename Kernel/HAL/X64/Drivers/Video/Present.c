/*
 * Present.c — HAL Present 编排（gui-g6-present；条带 blit 见 PresentRect.c）
 *
 * 两层脏提醒：此处是 HAL DirtyUnion → GOP blit；Gui MarkDamage 在 Compose/Damage.c。
 *
 * ── Present vs Flush（对表拧，勿凭感觉改）──
 *
 * HalVideoPresent / VideoPresent
 *   · 至多推进「一轮」内容脏 + 光标脏（各走 PresentRectRows）
 *   · 内容 Partial（FbBytes 边界 / 条带未跑完）→ 立刻 return，留下 Dirty 待下轮
 *   · 调用方：Shell 环末（!GuiPresentBlocked）；光标/拖窗部分路径；Defer Pop 等
 *   · 真机：单次 cli memcpy 过久 → xHCI poll 饿死；故必须条带 + 条间开中断
 *
 * HalVideoPresentFlush / VideoPresentFlush
 *   · 循环 Present 直到两脏皆空（Guard 上限）；仍脏则并整屏再刷
 *   · 调用方：Compose/Damage/Refresh/Theme 末（ComposeBusy 内）；关窗轻量 PresentFlush
 *   · 用途：合成结束保证任务栏/底边进 GOP，避免「半栏洞」
 *
 * 分矩形：gDirty（内容）与 gCurDirty（光标）分开 Present，避免 AABB 并成近全屏。
 * Partial 坑：真机 Pitch>Width 时勿用过小 FrameBufferSize 裁 Y——中下部永不前进
 *   → 栏/底边永远不 Present（见下方 FbBytes=max(Size, Pitch*H*4)）。
 *
 * 条带：PRESENT_CHUNK_ROWS（默认 64）；拖窗可 SetPresentChunkRows 放大减频闪。
 * 门闩（Gui 侧 G1）：DeferPresent / GuiPresentBlocked / ShellEchoCoalesce — Compose.c 头。
 * Shell 契约：Present 前抽 RX — Shell.c 头（runtime-e-shell）。
 */
#include "Private.h"
#include "HalDriverSched.h"

/* 单次 Present 条带行数：cli 下 memcpy 过久会饿死 xHCI poll/MSI */
void DirtyUnionInto(int *Dirty, UINT32 *Dx0, UINT32 *Dy0, UINT32 *Dx1,
                           UINT32 *Dy1, UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    UINT32 X1;
    UINT32 Y1;

    if (gForceFront) {
        return;
    }
    if (!W || !H || gScreen.Width == 0 || gScreen.Height == 0) {
        return;
    }
    if (X >= gScreen.Width || Y >= gScreen.Height) {
        return;
    }
    X1 = X + W;
    Y1 = Y + H;
    if (X1 > gScreen.Width) {
        X1 = gScreen.Width;
    }
    if (Y1 > gScreen.Height) {
        Y1 = gScreen.Height;
    }
    if (X >= X1 || Y >= Y1) {
        return;
    }
    if (!*Dirty) {
        *Dx0 = X;
        *Dy0 = Y;
        *Dx1 = X1;
        *Dy1 = Y1;
        *Dirty = 1;
        return;
    }
    if (X < *Dx0) {
        *Dx0 = X;
    }
    if (Y < *Dy0) {
        *Dy0 = Y;
    }
    if (X1 > *Dx1) {
        *Dx1 = X1;
    }
    if (Y1 > *Dy1) {
        *Dy1 = Y1;
    }
}

void DirtyUnion(UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    if (gCursorOverlay) {
        DirtyUnionInto(&gCurDirty, &gCx0, &gCy0, &gCx1, &gCy1, X, Y, W, H);
        return;
    }
    DirtyUnionInto(&gDirty, &gDx0, &gDy0, &gDx1, &gDy1, X, Y, W, H);
}

void DirtyUnionCursor(UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    DirtyUnionInto(&gCurDirty, &gCx0, &gCy0, &gCx1, &gCy1, X, Y, W, H);
}

void VideoCursorOverlayBegin(void) {
    gCursorOverlay = 1;
}

void VideoCursorOverlayEnd(void) {
    gCursorOverlay = 0;
}

void VideoPresent(void) {
    UINT32 X0;
    UINT32 Y0;
    UINT32 X1;
    UINT32 Y1;
    UINT64 FbBytes;
    UINT64 LayoutBytes;
    int Partial;

    if (!gBackOn || !gBack || !gFront) {
        gDirty = 0;
        gCurDirty = 0;
        return;
    }
    if (!gDirty && !gCurDirty) {
        return;
    }
    /* PR-K-preempt-cs：条带 cli 岛禁切（条间仍可 Restore IF） */
    HalDriverPreemptDisable();
    /*
     * 内容与光标分矩形 Present，避免 AABB 并成近全屏。
     * 大块按行条带 cli，条间开中断（禁止长 cli 饿死 USB；光标契约见 G7）。
     */
    LayoutBytes = (UINT64)gFrontPitch * (UINT64)gPhysH * 4ull;
    if (gPhysH == 0) {
        LayoutBytes = (UINT64)gFrontPitch * (UINT64)gScreen.Height * 4ull;
    }
    /*
     * 真机 GOP 常报 Size=Width*Height*4，但 Pitch>Width。
     * 若仍用 Size 做边界，Y 到中下部就 Partial 且永不前进 → 任务栏/底边永远不 Present。
     */
    FbBytes = gScreen.FrameBufferSize;
    if (FbBytes < LayoutBytes) {
        FbBytes = LayoutBytes;
    }

    if (gDirty) {
        X0 = gDx0;
        Y0 = gDy0;
        X1 = gDx1;
        Y1 = gDy1;
        gDirty = 0;
        if (X0 < gScreen.Width && Y0 < gScreen.Height) {
            if (X1 > gScreen.Width) {
                X1 = gScreen.Width;
            }
            if (Y1 > gScreen.Height) {
                Y1 = gScreen.Height;
            }
            PresentRectRows(X0, Y0, X1, Y1, FbBytes, &gDirty, &gDx0, &gDy0,
                            &gDx1, &gDy1, &Partial);
            if (Partial) {
                HalDriverPreemptEnable();
                return;
            }
        }
    }

    if (gCurDirty) {
        X0 = gCx0;
        Y0 = gCy0;
        X1 = gCx1;
        Y1 = gCy1;
        gCurDirty = 0;
        if (X0 < gScreen.Width && Y0 < gScreen.Height) {
            if (X1 > gScreen.Width) {
                X1 = gScreen.Width;
            }
            if (Y1 > gScreen.Height) {
                Y1 = gScreen.Height;
            }
            PresentRectRows(X0, Y0, X1, Y1, FbBytes, &gCurDirty, &gCx0, &gCy0,
                            &gCx1, &gCy1, &Partial);
        }
    }
    HalDriverPreemptEnable();
}

void VideoPresentFlush(void) {
    UINTN Guard;

    for (Guard = 0; Guard < 8192u; Guard++) {
        if (!gDirty && !gCurDirty) {
            return;
        }
        VideoPresent();
    }
    /* 仍脏：强制整屏再刷一轮（避免 4K 半截留下任务栏空洞） */
    if (gDirty || gCurDirty) {
        DirtyUnion(0, 0, gScreen.Width, gScreen.Height);
        for (Guard = 0; Guard < 8192u; Guard++) {
            if (!gDirty && !gCurDirty) {
                return;
            }
            VideoPresent();
        }
    }
}

void VideoSetPresentChunkRows(UINT32 Rows) {
    /* 0=默认；禁止全 1 作「无限」——与 Y 相加会回绕死循环 */
    if (Rows == 0xFFFFFFFFu) {
        Rows = 16384u;
    }
    gPresentChunkRows = Rows;
}

void VideoDiscardDirty(void) {
    gDirty = 0;
    gCurDirty = 0;
}
