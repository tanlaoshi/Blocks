/*
 * PresentRect.c — Present 条带 blit（gui-g6-present）
 *
 * 被 VideoPresent 调用：按 PRESENT_CHUNK_ROWS（或 Set 值）cli 拷一行带，
 * 条间 HalIrqRestore + 抽串口软环。OutPartial=1 时上层留下 Dirty 待下轮。
 * 编排 / Flush 语义见 Present.c 头。
 *
 * PR-23a：脏矩形 / gClip* 仍是逻辑坐标。后缓冲尺寸为 gDrawW×gDrawH：
 *   - 已等于物理尺寸（200% 按物理绘制，或 100% 逻辑=物理）→ 逻辑脏区 ×
 *     gDrawScale 后整行 1:1 memcpy，不再最近邻放大；
 *   - 其它档位 gDrawScale=1，仍按逻辑脏区最近邻放大，行为与改动前一致。
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
    int Scaled = (gUiScale != 100u) || (gScreen.Width != gPhysW) ||
                 (gScreen.Height != gPhysH);
    UINT32 S = DrawScale();
    UINT32 DrawW = gDrawW ? gDrawW : gScreen.Width;
    UINT32 DrawH = gDrawH ? gDrawH : gScreen.Height;
    /* 后缓冲已经是物理分辨率：可整行 1:1 拷贝 */
    int Direct = (DrawW != 0 && DrawH != 0 &&
                  DrawW == gPhysW && DrawH == gPhysH);
    UINT32 ChunkRows = gPresentChunkRows;

    if (ChunkRows == 0) {
        ChunkRows = PRESENT_CHUNK_ROWS;
    }

    *OutPartial = 0;
    if (X0 >= X1 || Y0 >= Y1) {
        return;
    }

    if (Scaled) {
        HalIgpuNotePresentSkipScale();
    }

    if (Direct) {
        /*
         * 逻辑脏区 → 绘制/物理脏区；按物理行条带 cli memcpy。
         * S=1 且逻辑=物理时与旧 1:1 路径逐字一致。
         */
        UINT32 Px0 = X0 * S;
        UINT32 Py0 = Y0 * S;
        UINT32 Px1 = X1 * S;
        UINT32 Py1 = Y1 * S;

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
         * igpu-4：S=1 大矩形且 blit 就绪时一次 SRC_COPY；失败仍走 memcpy。
         * S>1 时后缓冲已是物理尺寸，按 PR-23a 固定走 CPU 1:1 memcpy。
         */
        if (S == 1u && gBack && gBackOn && HalIgpuReady()
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
                    *Dy0 = Y / S;
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
        return;
    }

    /* 最近邻：逻辑脏区 → 物理矩形；按物理行条带 cli */
    {
        UINT32 Px0;
        UINT32 Py0;
        UINT32 Px1;
        UINT32 Py1;
        UINT32 Py;
        UINT32 ChunkPy;
        UINT32 DrawX0;
        UINT32 DrawX1;
        UINT32 DrawY0;
        UINT32 DrawY1;

        if (gPhysW == 0 || gPhysH == 0 || DrawW == 0 || DrawH == 0) {
            return;
        }
        /* 逻辑脏区先换成绘制坐标，再按绘制尺寸→物理尺寸的比例映射 */
        DrawX0 = X0 * S;
        DrawY0 = Y0 * S;
        DrawX1 = X1 * S;
        DrawY1 = Y1 * S;
        /*
         * 上取整 + 外扩 8 物理像素：150%/200% 最近邻时否则光标/图标拖尾。
         */
        Px0 = (UINT32)(((UINT64)DrawX0 * (UINT64)gPhysW) / (UINT64)DrawW);
        Py0 = (UINT32)(((UINT64)DrawY0 * (UINT64)gPhysH) / (UINT64)DrawH);
        Px1 = (UINT32)((((UINT64)DrawX1 * (UINT64)gPhysW) + (UINT64)DrawW - 1u) /
                       (UINT64)DrawW);
        Py1 = (UINT32)((((UINT64)DrawY1 * (UINT64)gPhysH) + (UINT64)DrawH - 1u) /
                       (UINT64)DrawH);
        if (Px0 >= 8u) {
            Px0 -= 8u;
        } else {
            Px0 = 0;
        }
        if (Py0 >= 8u) {
            Py0 -= 8u;
        } else {
            Py0 = 0;
        }
        if (Px1 + 8u < gPhysW) {
            Px1 += 8u;
        } else {
            Px1 = gPhysW;
        }
        if (Py1 + 8u < gPhysH) {
            Py1 += 8u;
        } else {
            Py1 = gPhysH;
        }
        if (Px0 >= Px1 || Py0 >= Py1) {
            return;
        }
        Py = Py0;
        while (Py < Py1) {
            if (ChunkRows >= (Py1 - Py)) {
                ChunkPy = Py1;
            } else {
                ChunkPy = Py + ChunkRows;
            }
            Flags = HalIrqSave();
            for (; Py < ChunkPy; Py++) {
                UINT32 Ly;
                UINT32 Px;
                UINT32 *Dst;

                Ly = (UINT32)(((UINT64)Py * (UINT64)DrawH) /
                              (UINT64)gPhysH);
                if (Ly >= DrawH) {
                    Ly = DrawH - 1;
                }
                RowOff = ((UINT64)Py * (UINT64)gFrontPitch + (UINT64)Px0) * 4ull;
                if (RowOff + (UINT64)(Px1 - Px0) * 4ull > FbBytes) {
                    /* 映射回逻辑脏区续传 */
                    *Dx0 = X0;
                    *Dy0 = Ly / S;
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
                Dst = &gFront[Py * gFrontPitch + Px0];
                for (Px = Px0; Px < Px1; Px++) {
                    UINT32 Lx = (UINT32)(((UINT64)Px * (UINT64)DrawW) /
                                         (UINT64)gPhysW);
                    if (Lx >= DrawW) {
                        Lx = DrawW - 1;
                    }
                    Dst[Px - Px0] = gBack[Ly * gBackPitch + Lx];
                }
            }
            HalIrqRestore(Flags);
            if (HalSerialPresent()) {
                HalSerialRxPump();
            }
        }
    }
}
