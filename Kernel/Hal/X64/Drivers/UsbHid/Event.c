/*
 * Event.c — xHCI 事件环处理（传输完成、命令完成）
 *
 * 【初学者】
 * - UsbHid 子模块：EventRingPoll；更新 gCommandDone / gTransferDone。
 */
#include "Internal.h"

void InterruptQueue(USB_HID_DEVICE *Device) {
    UINT32 Length;

    if (Device == 0 || Device->SlotIdentifier == 0 || Device->InterruptDeviceContextIndex == 0 || Device->InterruptRing == 0) {
        return;
    }
    Length = Device->MaxPacketSize;
    if (Length == 0 || Length > 8u) {
        Length = 8;
    }
    Device->ReportReady = 0;
    RingEnqueue(Device->InterruptRing, &Device->Interrupt, PhysicalAddress(Device->Report), Length,
            TRB_TYPE(TRB_NORMAL) | TRB_IOC | TRB_ISP);
    DoorbellRing(Device->SlotIdentifier, Device->InterruptDeviceContextIndex);
}

static void HandleTransfer(TRANSFER_REQUEST_BLOCK *Event) {
    UINT32 Code = (Event->Status >> 24) & 0xFFu;
    UINT32 EndpointContext = (Event->Control >> 16) & 0x1Fu;
    UINT32 Slot = (Event->Control >> 24) & 0xFFu;
    UINT64 TransferRequestBlockPointer = Event->Parameter & ~0xFULL;
    UINT32 Remain = Event->Status & 0xFFFFFFu;
    UINT32 TransferLength;
    int Ok = (Code == CC_SUCCESS || Code == CC_SHORT_PACKET);

    if (gTransferDevice != 0 && Slot == gTransferDevice->SlotIdentifier && EndpointContext == 1u) {
        gTransferCode = Code;
        gTransferDone = 1;
        return;
    }

    if (gKeyboard.SlotIdentifier != 0 && gKeyboard.InterruptDeviceContextIndex != 0 && EndpointContext != 1u &&
        ((Slot == gKeyboard.SlotIdentifier && EndpointContext == gKeyboard.InterruptDeviceContextIndex) ||
         (TransferRequestBlockPointer >= PhysicalAddress(gKeyboard.InterruptRing) &&
          TransferRequestBlockPointer < PhysicalAddress(gKeyboard.InterruptRing) + sizeof(TRANSFER_REQUEST_BLOCK) * TRANSFER_RING_SIZE))) {
        if (Ok) {
            TransferLength = (gKeyboard.MaxPacketSize > Remain) ? (gKeyboard.MaxPacketSize - Remain) : 8u;
            if (TransferLength > 8u) {
                TransferLength = 8;
            }
            ReportKeyboard(gKeyboard.Report);
        }
        InterruptQueue(&gKeyboard);
        return;
    }

    if (gMouse.SlotIdentifier != 0 && gMouse.InterruptDeviceContextIndex != 0 && EndpointContext != 1u &&
        ((Slot == gMouse.SlotIdentifier && EndpointContext == gMouse.InterruptDeviceContextIndex) ||
         (TransferRequestBlockPointer >= PhysicalAddress(gMouse.InterruptRing) &&
          TransferRequestBlockPointer < PhysicalAddress(gMouse.InterruptRing) + sizeof(TRANSFER_REQUEST_BLOCK) * TRANSFER_RING_SIZE))) {
        if (Ok) {
            TransferLength = (gMouse.MaxPacketSize > Remain) ? (gMouse.MaxPacketSize - Remain) : 8u;
            if (TransferLength > 8u) {
                TransferLength = 8;
            }
            ReportMouse(&gMouse, (UINT8)TransferLength);
        }
        InterruptQueue(&gMouse);
    }
}

void EventProcess(void) {
    int Progress = 0;
    int Guard = 0;

    for (;;) {
        TRANSFER_REQUEST_BLOCK *Event;
        UINT32 Type;

        if (++Guard > (int)(EVENT_RING_SIZE * 2u + 8u)) {
            break;
        }
        Event = &gEventRing[gEventDequeue];
        if ((Event->Control & TRB_C) != gEventConsumerCycleState) {
            break;
        }
        Progress = 1;
        Type = TransferRequestBlockType(Event->Control);
        if (Type == TRB_CMD_COMPLETION) {
            gCommandCode = (Event->Status >> 24) & 0xFFu;
            gCommandSlot = (Event->Control >> 24) & 0xFFu;
            gCommandDone = 1;
        } else if (Type == TRB_TRANSFER_EVENT) {
            HandleTransfer(Event);
        }
        gEventDequeue++;
        if (gEventDequeue == EVENT_RING_SIZE) {
            gEventDequeue = 0;
            gEventConsumerCycleState ^= 1u;
        }
    }
    if (Progress) {
        Write64(gRuntimeBase + 0x38u, PhysicalAddress(&gEventRing[gEventDequeue]) | (1ULL << 3));
        Write32(gOperationalBase + 4u, USBSTS_EINT);
    }
}
