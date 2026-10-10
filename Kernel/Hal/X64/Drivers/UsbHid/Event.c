/*
 * Event.c — 事件环消化（命令完成 / EP0 / 中断 IN）
 */
#include "Internal.h"

void QueueInterrupt(USB_HID_DEVICE *D) {
    UINT32 Len;

    if (D == 0 || D->SlotId == 0 || D->IntrDci == 0 || D->IntrRing == 0) {
        return;
    }
    Len = D->Mps;
    if (Len == 0 || Len > 8u) {
        Len = 8;
    }
    D->ReportReady = 0;
    Enqueue(D->IntrRing, &D->Intr, Phys(D->Report), Len,
            TRB_TYPE(TRB_NORMAL) | TRB_IOC | TRB_ISP);
    RingDoorbell(D->SlotId, D->IntrDci);
}

static void HandleTransfer(TRANSFER_REQUEST_BLOCK *Evt) {
    UINT32 Code = (Evt->Status >> 24) & 0xFFu;
    UINT32 Ep = (Evt->Control >> 16) & 0x1Fu;
    UINT32 Slot = (Evt->Control >> 24) & 0xFFu;
    UINT64 TrbPtr = Evt->Parameter & ~0xFULL;
    UINT32 Remain = Evt->Status & 0xFFFFFFu;
    UINT32 XferLen;
    int Ok = (Code == CC_SUCCESS || Code == CC_SHORT_PACKET);

    if (gTransferDevice != 0 && Slot == gTransferDevice->SlotId && Ep == 1u) {
        gXferCode = Code;
        gXferDone = 1;
        return;
    }

    if (gKbd.SlotId != 0 && gKbd.IntrDci != 0 && Ep != 1u &&
        ((Slot == gKbd.SlotId && Ep == gKbd.IntrDci) ||
         (TrbPtr >= Phys(gKbd.IntrRing) &&
          TrbPtr < Phys(gKbd.IntrRing) + sizeof(TRANSFER_REQUEST_BLOCK) * RING_SIZE))) {
        if (Ok) {
            XferLen = (gKbd.Mps > Remain) ? (gKbd.Mps - Remain) : 8u;
            if (XferLen > 8u) {
                XferLen = 8;
            }
            ReportKeyboard(gKbd.Report);
        }
        QueueInterrupt(&gKbd);
        return;
    }

    if (gMouse.SlotId != 0 && gMouse.IntrDci != 0 && Ep != 1u &&
        ((Slot == gMouse.SlotId && Ep == gMouse.IntrDci) ||
         (TrbPtr >= Phys(gMouse.IntrRing) &&
          TrbPtr < Phys(gMouse.IntrRing) + sizeof(TRANSFER_REQUEST_BLOCK) * RING_SIZE))) {
        if (Ok) {
            XferLen = (gMouse.Mps > Remain) ? (gMouse.Mps - Remain) : 8u;
            if (XferLen > 8u) {
                XferLen = 8;
            }
            ReportMouse(&gMouse, (UINT8)XferLen);
        }
        QueueInterrupt(&gMouse);
    }
}

void ProcessEvents(void) {
    int Progress = 0;
    int Guard = 0;

    for (;;) {
        TRANSFER_REQUEST_BLOCK *Evt;
        UINT32 Type;

        if (++Guard > (int)(EVT_SIZE * 2u + 8u)) {
            break;
        }
        Evt = &gEvtRing[gEvtDeq];
        if ((Evt->Control & TRB_C) != gEvtCcs) {
            break;
        }
        Progress = 1;
        Type = TransferRequestBlockType(Evt->Control);
        if (Type == TRB_CMD_COMPLETION) {
            gCmdCode = (Evt->Status >> 24) & 0xFFu;
            gCmdSlot = (Evt->Control >> 24) & 0xFFu;
            gCmdDone = 1;
        } else if (Type == TRB_TRANSFER_EVENT) {
            HandleTransfer(Evt);
        }
        gEvtDeq++;
        if (gEvtDeq == EVT_SIZE) {
            gEvtDeq = 0;
            gEvtCcs ^= 1u;
        }
    }
    if (Progress) {
        Wr64(gRt + 0x38u, Phys(&gEvtRing[gEvtDeq]) | (1ULL << 3));
        Wr32(gOp + 4u, USBSTS_EINT);
    }
}
