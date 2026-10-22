/*
 * FtdiTx.c — PR-H-usb-uart：FT232 Bulk OUT TX + 事件匹配（IN/OUT）
 */
#include "Compiler.h"
#include "XHCI/Serial/Ftdi/Private.h"

static UINT8 gFtdiTxBuf[64] BLOCKS_ALIGNED(64);
static volatile UINT32 gFtdiBulkDone;
static volatile UINT32 gFtdiBulkCode;
static int gFtdiTxBusy;

static int MatchTransfer(UINT32 EvtSlot, UINT32 Ep, UINT64 TrbPtr, UINT32 Code,
                           UINT32 Remain) {
    UINT64 OutLo;
    UINT64 OutHi;
    UINT64 InLo;
    UINT64 InHi;

    if (!gFtdiClaimed || gFtdiSlot == 0 || EvtSlot != gFtdiSlot) {
        return 0;
    }
    OutLo = PointerToPhysical(gFtdiBulkOutRing);
    OutHi = OutLo + sizeof(gFtdiBulkOutRing);
    InLo = PointerToPhysical(gFtdiBulkInRing);
    InHi = InLo + sizeof(gFtdiBulkInRing);

    if ((gFtdiBulkOutDci != 0 && Ep == gFtdiBulkOutDci) ||
        (TrbPtr >= OutLo && TrbPtr < OutHi)) {
        gFtdiBulkCode = Code;
        gFtdiBulkDone = 1;
        return 1;
    }
    if ((gFtdiBulkInDci != 0 && Ep == gFtdiBulkInDci) ||
        (TrbPtr >= InLo && TrbPtr < InHi)) {
        return gFtdi.OnComplete ? gFtdi.OnComplete(Code, Remain) : 0;
    }
    return 0;
}

static int WaitBulk(void) {
    int Own = 0;
    int Result = -1;
    UINT64 T0 = ReadTsc();
    UINT64 Need = 200ULL * 3000000ULL;

    if (!EventIsExclusive()) {
        EventEnterExclusive();
        Own = 1;
    }
    for (;;) {
        if (HalCpuIsHypervisor()) {
            ProcessEvents();
        } else {
            ProcessEventsRealPc();
        }
        ServiceHidCompletions();
        if (gFtdiBulkDone) {
            Result = (gFtdiBulkCode == CC_SUCCESS || gFtdiBulkCode == CC_SHORT_PACKET)
                         ? 0
                         : -1;
            break;
        }
        if (ReadTsc() - T0 >= Need) {
            break;
        }
        __asm__ volatile("pause");
    }
    if (Own) {
        EventLeaveExclusive();
    }
    return Result;
}

static void Write(const char *Text) {
    UINT32 Mps;
    UINT32 N;

    if (!Text || !gFtdiClaimed || gFtdiSlot == 0 || gFtdiBulkOutDci == 0) {
        return;
    }
    if (gFtdiTxBusy) {
        return;
    }
    gFtdiTxBusy = 1;
    Mps = gFtdiBulkOutMps ? gFtdiBulkOutMps : 64;
    if (Mps > sizeof(gFtdiTxBuf)) {
        Mps = (UINT32)sizeof(gFtdiTxBuf);
    }

    while (*Text) {
        N = 0;
        while (Text[N] && N < Mps) {
            gFtdiTxBuf[N] = (UINT8)Text[N];
            N++;
        }
        Text += N;
        FlushDma(gFtdiTxBuf, N);
        gFtdiBulkDone = 0;
        gFtdiBulkCode = 0;
        EventEnterExclusive();
        Enqueue(gFtdiBulkOutRing, &gFtdiBulkOut, PointerToPhysical(gFtdiTxBuf), N,
                TRB_TYPE(TRB_NORMAL) | TRB_IOC);
        FlushDma(gFtdiBulkOutRing, sizeof(gFtdiBulkOutRing));
        RingDoorbell(gFtdiSlot, gFtdiBulkOutDci);
        if (WaitBulk() < 0) {
            EventLeaveExclusive();
            break;
        }
        EventLeaveExclusive();
    }
    gFtdiTxBusy = 0;
}

void FtdiFillTransmit(void) {
    gFtdi.MatchTransfer = MatchTransfer;
    gFtdiUart.Write = Write;
}
