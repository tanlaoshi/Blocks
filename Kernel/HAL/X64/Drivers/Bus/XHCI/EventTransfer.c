/*
 * EventTransfer.c — Transfer Event 分发（PR-F-xhci-3）
 *
 * 键鼠中断完成 → EventTransferHid.c（PR-NORM-11）。
 */
#include "XHCI/Private.h"

void EventHandleTransfer(XHCI_TRB *Evt) {
    UINT32 Code = (Evt->Status >> 24) & 0xFF;
    UINT32 Endpoint = (Evt->Control >> 16) & 0x1F;
    UINT32 EvtSlot = (Evt->Control >> 24) & 0xFF;
    UINT64 TrbPtr = Evt->Parameter & ~0xFULL;
    UINT32 Remain = Evt->Status & XHCI_TRB_REMAIN_MASK;
    int Matched = 0;

    gStatTransferAny++;
    gStatLastCc = Code;
    gStatLastSlot = EvtSlot;
    gStatLastEndpoint = Endpoint;

    /* EP0(DCI=1) 才唤醒 WaitTransfer，避免 HID IN 误完成 EP0 等待 */
    if (EvtSlot == gTransferSlot && Endpoint == 1) {
        gTransferCode = Code;
        gTransferRemain = Remain;
        gTransferDone = 1;
        Matched = 1; /* GET_REPORT/控制传输：勿记入 unmatched 刷屏 */
    }
    /* PR-H-msc-5：Bulk 完成（slot+DCI 或 TRB 落在 Bulk 环） */
    if (!Matched && gMscScanSlot != 0 && EvtSlot == gMscScanSlot &&
        ((gMscBulkInDci != 0 && Endpoint == gMscBulkInDci) ||
         (gMscBulkOutDci != 0 && Endpoint == gMscBulkOutDci))) {
        gBulkCode = Code;
        gBulkRemain = Remain;
        gBulkDone = 1;
        Matched = 1;
    }
    if (!Matched && gFtdi.MatchTransfer &&
        gFtdi.MatchTransfer(EvtSlot, Endpoint, TrbPtr, Code, Remain)) {
        Matched = 1;
    }
    if (!Matched && gCdc.MatchTransfer &&
        gCdc.MatchTransfer(EvtSlot, Endpoint, TrbPtr, Code, Remain)) {
        Matched = 1;
    }
    if (!Matched &&
        BluetoothMatchTransfer(EvtSlot, Endpoint, TrbPtr, Code, Remain)) {
        Matched = 1;
    }
    if (!Matched) {
        UINT64 BulkInLo = PointerToPhysical(gBulkInRing);
        UINT64 BulkInHi = BulkInLo + sizeof(gBulkInRing);
        UINT64 BulkOutLo = PointerToPhysical(gBulkOutRing);
        UINT64 BulkOutHi = BulkOutLo + sizeof(gBulkOutRing);

        if ((TrbPtr >= BulkInLo && TrbPtr < BulkInHi) ||
            (TrbPtr >= BulkOutLo && TrbPtr < BulkOutHi)) {
            gBulkCode = Code;
            gBulkRemain = Remain;
            gBulkDone = 1;
            Matched = 1;
        }
    }
    if (EventHandleTransferHid(Code, Endpoint, EvtSlot, TrbPtr, Remain)) {
        Matched = 1;
    }
    if (!Matched) {
        gStatUnmatched++;
        if (gDiagTransferLogged < 8) {
            char Line[80];
            int N = 0;
            const char *P = "Boot: XHCI xfer s=";
            while (*P && N < 24) {
                Line[N++] = *P++;
            }
            Line[N++] = (char)('0' + ((EvtSlot / 10) % 10));
            Line[N++] = (char)('0' + (EvtSlot % 10));
            Line[N++] = ' ';
            Line[N++] = 'e';
            Line[N++] = '=';
            Line[N++] = (char)('0' + ((Endpoint / 10) % 10));
            Line[N++] = (char)('0' + (Endpoint % 10));
            Line[N++] = ' ';
            Line[N++] = 'c';
            Line[N++] = '=';
            Line[N++] = (char)('0' + ((Code / 10) % 10));
            Line[N++] = (char)('0' + (Code % 10));
            Line[N++] = '\n';
            Line[N] = 0;
            LogUsb(Line);
            gDiagTransferLogged++;
        }
    }
}
