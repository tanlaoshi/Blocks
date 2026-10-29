/*
 * PresentRect.c — Present 条带 blit（gui-g6-present）
 *
 * 被 VideoPresent 调用：按 PRESENT_CHUNK_ROWS（或 Set 值）cli 拷一行带，
 * 条间 HalIrqRestore + 抽串口软环。OutPartial=1 时上层留下 Dirty 待下轮。
 * 编排 / Flush 语义见 Present.c 头。
 *
 * PR-23E：后缓冲恒为物理尺寸（gDrawW×gDrawH == 物理宽高），所以 Present
 * 只剩「1:1 整行 memcpy」一条路——最近邻放大分支已删除。脏矩形 / gClip*
 * 仍是逻辑坐标，这里按有理数比例换成物理矩形：S=1（100%）时与旧的 1:1
 * 路径逐字节相同。
 */
#include "Private.h"
#include "HalDevices.h"
#include "HalSerial.h"

extern void *memcpy(void *Dst, const void *Src, UINTN Len);

/* 将脏区 blit 到 GOP；无后缓冲时为空操作。 */
void PresentRectRows(UINT32 X0, UINT32 Y0, UINT32 X1, UINT32 Y1,
                     UINT64 FbBytes, int *DirtyOut, UINT32 *Dx0,
                     UINT32 *Dy0, UINT32 *Dx1, UINT32 *Dy1,
                     int *OutPartial) {
    UINT32 Y;
    UINT32 ChunkEnd;
    UINT64 RowOff;
    UINT64 RowBytes;
    UINT64 Flags;
    UINT32 DrawW = DrawWidth();
    UINT32 DrawH = DrawHeight();
    UINT32 ChunkRows = gPresentChunkRows;
    UINT32 Px0;
    UINT32 Py0;
    UINT32 Px1;
    UINT32 Py1;

    if (ChunkRows == 0) {
        ChunkRows = PRESENT_CHUNK_ROWS;
    }

    *OutPartial = 0;
    if (X0 >= X1 || Y0 >= Y1) {
        return;
    }
    if (DrawW != gPhysW || DrawH != gPhysH || gPhysW == 0 || gPhysH == 0) {
        /* 后缓冲不是物理尺寸：本 PR 之后不该出现，宁可少画也不放大上屏 */
        return;
    }
    if (gUiScale != 100u || gScreen.Width != gPhysW ||
        gScreen.Height != gPhysH) {
        HalIgpuNotePresentSkipScale();
    }

    /*
     * 逻辑脏区 → 物理脏区：两端点各自换算（端点相减），相邻脏区接缝不会
     * 因取整而漏掉一条物理行/列。50% 下逻辑列可能塌成 0 宽，仍要保证至少
     * 写 1 像素（画的时候就是这个规矩，见 PixelSpanX）。
     */
    Px0 = ScaleX(X0);
    Py0 = ScaleY(Y0);
    Px1 = ScaleEndX(X1);
    Py1 = ScaleEndY(Y1);
    if (Px1 <= Px0) {
        Px1 = Px0 + 1u;
    }
    if (Py1 <= Py0) {
        Py1 = Py0 + 1u;
    }
    if (Px0 >= DrawW || Py0 >= DrawH) {
        return;
    }
    if (Px1 > DrawW) {
        Px1 = DrawW;
    }
    if (Py1 > DrawH) {
        Py1 = DrawH;
    }
    if (Px0 >= Px1 || Py0 >= Py1) {
        return;
    }
    RowBytes = (UINT64)(Px1 - Px0) * 4ull;
    /*
     * igpu-4：只有 100%（逻辑=物理、1:1 memcpy）才走 GPU SRC_COPY。
     * 其它比例下后缓冲虽然也是物理尺寸，但该路径此前从未在缩放档验证过，
     * 保持「缩放档固定 CPU 1:1 memcpy」的既定策略。
     */
    if (gDrawNum == 1u && gDrawDen == 1u && gBack && gBackOn && HalIgpuReady()
        && ((UINT64)(Px1 - Px0) * (UINT64)(Py1 - Py0)
            >= (UINT64)HalIgpuBlitMinPixels())
        && HalIgpuPresentRect(gBack, gBackPitch, gFrontPitch, DrawH,
                           Px0, Py0, Px1, Py1)) {
        return;
    }
    Y = Py0;
    while (Y < Py1) {
        /* 勿 Y+ChunkRows：ChunkRows=0xFFFFFFFF 会回绕 → Y 不前进死循环 */
        if (ChunkRows >= (Py1 - Y)) {
            ChunkEnd = Py1;
        } else {
            ChunkEnd = Y + ChunkRows;
        }
        Flags = HalIrqSave();
        for (; Y < ChunkEnd; Y++) {
            const UINT32 *Src;
            UINT32 *Dst;

            RowOff = ((UINT64)Y * (UINT64)gFrontPitch + (UINT64)Px0) * 4ull;
            if (RowOff + RowBytes > FbBytes) {
                /* 映射回逻辑脏区续传 */
                *Dx0 = X0;
                *Dy0 = UnscaleY(Y);
                if (*Dy0 >= Y1) {
                    *Dy0 = Y1 - 1u;
                }
                *Dx1 = X1;
                *Dy1 = Y1;
                *DirtyOut = 1;
                *OutPartial = 1;
                HalIrqRestore(Flags);
                return;
            }
            Src = &gBack[Y * gBackPitch + Px0];
            Dst = &gFront[Y * gFrontPitch + Px0];
            memcpy(Dst, Src, (UINTN)RowBytes);
        }
        HalIrqRestore(Flags);
        /* 条带间抽 COM1→软环（IRQ 未开时）；勿 SchedulerIoBreath——会 GuiPoll→Present 重入 */
        if (HalSerialPresent()) {
            HalSerialRxPump();
        }
    }
}
