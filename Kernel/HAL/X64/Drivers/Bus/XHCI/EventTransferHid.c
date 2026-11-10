/*
 * EventTransferHid.c — Transfer Event 键鼠中断完成（自 EventTransfer.c 纯搬 · PR-NORM-11）
 *
 * 中断 EP：只认 slot+DCI，或完成 TRB 落在中断环内。
 * 勿用 EndpointNum（易与 EP0 的 EndpointID=1 撞）或报告缓冲指针
 * （GET_REPORT 数据 TRB 也指向报告区 → 假 i=、干扰推送）。
 * Stop EP 副作用：勿 gIntrDone/QueueIntr，否则 Arm 同步时会在 SetTrDeq 前再敲门铃。
 */
#include "XHCI/Private.h"

int EventHandleTransferHid(UINT32 Code, UINT32 Endpoint, UINT32 EvtSlot,
                           UINT64 TrbPtr, UINT32 Remain) {
    UINT64 KeyboardLow = PointerToPhysical(gIntrRing);
    UINT64 KeyboardHigh = KeyboardLow + sizeof(gIntrRing);
    UINT64 MouseLo = PointerToPhysical(gMouseIntrRing);
    UINT64 MouseHi = MouseLo + sizeof(gMouseIntrRing);
    int Matched = 0;
    int KeyboardHit;
    int MouseHit;

    KeyboardHit = (gSlotId != 0 && gIntrDci != 0 && Endpoint != 1 &&
              ((EvtSlot == gSlotId && Endpoint == gIntrDci) ||
               (TrbPtr >= KeyboardLow && TrbPtr < KeyboardHigh)));
    MouseHit = (gMouseSlotId != 0 && gMouseIntrDci != 0 && Endpoint != 1 &&
                ((EvtSlot == gMouseSlotId && Endpoint == gMouseIntrDci) ||
                 (TrbPtr >= MouseLo && TrbPtr < MouseHi)));
    if (KeyboardHit) {
        Matched = 1;
        if (Code == CC_SUCCESS || Code == CC_SHORT_PACKET) {
            gStatIntrEvt++;
            gIntrReportReady = 1;
            gIntrDone = 1;
        } else if (Code == CC_STOPPED || Code == CC_STOPPED_LEN ||
                   Code == CC_STOPPED_SHORT) {
            /*
             * Stop EP 的副作用：勿 gIntrDone/QueueIntr，否则 Arm 同步时
             * 会在 SetTrDeq 前再敲门铃 → Context State Error (0x13)。
             */
        } else {
            gStatIntrEvt++;
            gIntrDone = 1; /* 其它错误：允许重投 */
            if (gDiagIntrCcLogged < 4) {
                char Line[64];
                int N = 0;
                const char *P = "Boot: XHCI kbd-intr cc=";
                while (*P && N < 28) {
                    Line[N++] = *P++;
                }
                Line[N++] = (char)('0' + ((Code / 10) % 10));
                Line[N++] = (char)('0' + (Code % 10));
                Line[N++] = '\n';
                Line[N] = 0;
                LogUsb(Line);
                gDiagIntrCcLogged++;
            }
        }
    }
    if (MouseHit) {
        Matched = 1;
        if (Code == CC_SUCCESS || Code == CC_SHORT_PACKET) {
            UINT32 Req = gMouseReportLen ? gMouseReportLen : 8;

            gStatMouseEvt++;
            gMouseReportReady = 1;
            gMouseIntrDone = 1;
            /* 短包：Remain=未传完；实际长度=请求-Remain */
            if (Remain < Req) {
                gMouseTransferLength = (UINT8)(Req - Remain);
            } else {
                gMouseTransferLength = (UINT8)Req;
            }
            if (gMouseTransferLength < 3) {
                gMouseTransferLength = 3;
            }
            if (gMouseTransferLength > 8) {
                gMouseTransferLength = 8;
            }
        } else if (Code == CC_STOPPED || Code == CC_STOPPED_LEN ||
                   Code == CC_STOPPED_SHORT) {
            /* 同上：Stop 取消，勿重投门铃 */
        } else {
            gStatMouseEvt++;
            gMouseIntrDone = 1;
            if (gDiagIntrCcLogged < 4) {
                char Line[64];
                int N = 0;
                const char *P = "Boot: XHCI mouse-intr cc=";
                while (*P && N < 30) {
                    Line[N++] = *P++;
                }
                Line[N++] = (char)('0' + ((Code / 10) % 10));
                Line[N++] = (char)('0' + (Code % 10));
                Line[N++] = '\n';
                Line[N] = 0;
                LogUsb(Line);
                gDiagIntrCcLogged++;
            }
        }
    }
    return Matched;
}
