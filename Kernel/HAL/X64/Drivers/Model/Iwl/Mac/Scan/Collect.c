/*
 * Collect.c — 扫描收包环编排（PR-F-iwl-2）
 * 单帧 / 失败诊断见 IwlScanCollect{Pkt,Diag}.c
 */
#include "Private.h"

int IwlScanCollect(void) {
    UINT32 I;
    IWL_RX_PKT *Pkt;
    UINTN Len;
    IWL_SCAN_COLLECT_CTX Ctx;

    IwlScanCollectContextInitialize(&Ctx);
    for (I = 0; I < 8000; I++) {
        IwlReceivePoll();
        while (IwlReceiveTake(&Pkt, &Len)) {
            if (IwlScanCollectOnPkt(&Ctx, Pkt, Len, I)) {
                return 1;
            }
        }
        IwlStallMilliseconds(1);
        if (Ctx.StopAt >= 0 && (int)I >= Ctx.StopAt) {
            break;
        }
    }
    IwlScanCollectLogFail(&Ctx);
    return 0;
}
