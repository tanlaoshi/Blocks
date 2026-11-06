/*
 * Receive.c — RX 环（PR-S-iwl-split-5）
 *
 * 8265 非 mqrx：RBD 是 u32(phys>>8)，不是 u64 全地址。
 */
#include "ReceiveInternal.h"
#include "PhysicalMemory.h"

UINT32 *gRbd;
UINT64 gRbdPhys;
UINT8 *gRbStts;
UINT64 gRbSttsPhys;
UINT8 *gReceiveBuffers[IWL_RX_Q_SIZE];
UINT64 gReceiveBufferPhysical[IWL_RX_Q_SIZE];
UINT32 gReceiveRead;
UINT32 gReceiveOffset; /* 当前 RB 内偏移（多帧） */
int gReceiveReady;

static void IwlZero(void *P, UINTN N) {
    UINT8 *B = (UINT8 *)P;
    UINTN i;

    for (i = 0; i < N; i++) {
        B[i] = 0;
    }
}

int IwlReceiveInitialize(void) {
    void *Mem;
    UINT32 i;
    UINT32 Cfg;

    if (!gReceiveReady) {
        /* RBD: 256×u32 ≈ 1KB；用 1 页 */
        Mem = PhysicalMemoryAllocatePages(1);
        if (!Mem) {
            return 0;
        }
        IwlZero(Mem, PAGE_SIZE);
        gRbd = (UINT32 *)Mem;
        gRbdPhys = (UINT64)(UINTN)Mem;

        Mem = PhysicalMemoryAllocatePages(1);
        if (!Mem) {
            return 0;
        }
        IwlZero(Mem, PAGE_SIZE);
        gRbStts = (UINT8 *)Mem;
        gRbSttsPhys = (UINT64)(UINTN)Mem;

        for (i = 0; i < IWL_RX_Q_SIZE; i++) {
            Mem = PhysicalMemoryAllocatePages(1);
            if (!Mem) {
                return 0;
            }
            IwlZero(Mem, PAGE_SIZE);
            gReceiveBuffers[i] = (UINT8 *)Mem;
            gReceiveBufferPhysical[i] = (UINT64)(UINTN)Mem;
            /* legacy FH：描述符 = 物理地址 >> 8 */
            gRbd[i] = (UINT32)(gReceiveBufferPhysical[i] >> 8);
        }
        gReceiveReady = 1;
    } else {
        /* 重绑前重置 RBD（可能被 HW 改写） */
        for (i = 0; i < IWL_RX_Q_SIZE; i++) {
            gRbd[i] = (UINT32)(gReceiveBufferPhysical[i] >> 8);
        }
        IwlZero(gRbStts, 16);
    }

    if (!IwlNicLock()) {
        return 0;
    }
    IwlMmioW32(IWL_FH_RCSR_CHNL0_CONFIG, 0);
    IwlMmioW32(IWL_FH_RSCSR_RBDCB_BASE, (UINT32)(gRbdPhys >> 8));
    IwlMmioW32(IWL_FH_RSCSR_STTS_WPTR, (UINT32)(gRbSttsPhys >> 4));
    IwlMmioW32(IWL_FH_RSCSR_RBDCB_WPTR, 0);
    IwlFlushDma(gRbd, IWL_RX_Q_SIZE * sizeof(UINT32));
    IwlFlushDma(gRbStts, 16);
    for (i = 0; i < IWL_RX_Q_SIZE; i++) {
        IwlFlushDma(gReceiveBuffers[i], PAGE_SIZE);
    }
    Cfg = IWL_FH_RCSR_EN | IWL_FH_RCSR_IRQ_HOST | IWL_FH_RCSR_RB_SIZE_4K
        | IWL_FH_RCSR_IGNORE_EMPTY
        | ((UINT32)IWL_RX_Q_SIZE_LOG << IWL_FH_RCSR_RBDCB_SIZE_POS);
    IwlMmioW32(IWL_FH_RCSR_CHNL0_CONFIG, Cfg);
    IwlNicUnlock();
    gReceiveRead = 0;
    gReceiveOffset = 0;
    IwlMmioW32(IWL_FH_RSCSR_RBDCB_WPTR, 8);
    return 1;
}

void IwlReceiveRestock(void) {
    UINT16 Closed;
    UINT32 Wptr;

    if (!gReceiveReady) {
        return;
    }
    /* OpenBSD：WPTR = closed_rb_num - 1（8 对齐） */
    IwlFlushDma(gRbStts, 16);
    Closed = (UINT16)(*(volatile UINT16 *)gRbStts) & 0xfffu;
    Closed &= IWL_RX_Q_MASK;
    Wptr = (Closed == 0) ? (IWL_RX_Q_SIZE - 1) : (UINT32)(Closed - 1);
    IwlMmioW32(IWL_FH_RSCSR_RBDCB_WPTR, Wptr & ~7u);
}

static UINT16 IwlRbClosed(void) {
    IwlFlushDma(gRbStts, 16);
    return (UINT16)(*(volatile UINT16 *)gRbStts) & 0xfffu;
}

void IwlReceivePoll(void) {
    UINT32 Int;

    if (!gReceiveReady || !gIwlBar) {
        return;
    }
    Int = IwlMmioR32(IWL_CSR_INT);
    if (Int) {
        IwlMmioW32(IWL_CSR_INT, Int);
        if (Int & IWL_CSR_INT_FH_RX) {
            IwlMmioW32(IWL_CSR_FH_INT_STATUS, 0x00FFFFFFu);
        }
    }
}

static void IwlReceiveAdvanceRb(void) {
    gReceiveOffset = 0;
    gReceiveRead = (gReceiveRead + 1) & IWL_RX_Q_MASK;
    IwlReceiveRestock();
}

int IwlReceiveTake(IWL_RX_PKT **OutPkt, UINTN *OutLen) {
    UINT16 Closed;
    UINT8 *Buf;
    IWL_RX_PKT *Pkt;
    UINTN Len;
    UINTN Step;

    if (!OutPkt || !OutLen || !gReceiveReady) {
        return 0;
    }

    for (;;) {
        Closed = IwlRbClosed() & IWL_RX_Q_MASK;
        if (gReceiveRead == Closed && gReceiveOffset == 0) {
            return 0;
        }
        /* 当前 RB 已开始解析但 closed 追上：仍把残余帧吃完 */
        if (gReceiveRead == Closed && gReceiveOffset != 0) {
            /* 无更多 HW 缓冲；收尾当前偏移后的 INVALID/越界 */
        }

        Buf = gReceiveBuffers[gReceiveRead];
        if (gReceiveOffset + sizeof(UINT32) + sizeof(IWL_CMD_HDR) > PAGE_SIZE) {
            IwlReceiveAdvanceRb();
            continue;
        }
        IwlFlushDma(Buf + gReceiveOffset, 64);
        Pkt = (IWL_RX_PKT *)(Buf + gReceiveOffset);
        if (Pkt->LenNFlags == IWL_RX_FRAME_INVALID) {
            IwlReceiveAdvanceRb();
            continue;
        }
        Len = (UINTN)(Pkt->LenNFlags & IWL_RX_FRAME_SIZE_MSK);
        /*
         * 刀 #41：最小合法帧 = cmd hdr（4B）。空载荷 ACK 的 Len==4，
         * 旧判 Len<8 会吞掉 paging/ant/dqa 回包（#40 n=00 而 r↑）。
         */
        if (Len < sizeof(IWL_CMD_HDR)) {
            IwlReceiveAdvanceRb();
            continue;
        }
        if (Len + sizeof(UINT32) > 64) {
            IwlFlushDma(Buf + gReceiveOffset, Len + sizeof(UINT32));
        }
        *OutPkt = Pkt;
        *OutLen = Len;
        Step = sizeof(UINT32) + Len;
        Step = (Step + IWL_RX_FRAME_ALIGN - 1u) & ~(IWL_RX_FRAME_ALIGN - 1u);
        gReceiveOffset += (UINT32)Step;
        /* 勿在此 AdvanceRb：调用方还握着 Pkt 指针 */
        return 1;
    }
}

/* 诊断：closed / read */
UINT32 IwlReceiveDiagClosed(void) {
    if (!gReceiveReady) {
        return 0xffffffffu;
    }
    return (UINT32)IwlRbClosed();
}

UINT32 IwlReceiveDiagRead(void) {
    return gReceiveRead;
}

