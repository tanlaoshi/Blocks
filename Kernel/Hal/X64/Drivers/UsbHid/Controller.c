/*
 * Controller.c — TakeLegacy + 建环 + Run（QEMU）
 */
#include "Internal.h"

static void TakeLegacy(void) {
    UINT32 Hcc1 = Read32(gCapabilityBase + 0x10u);
    UINT32 Xecp = (Hcc1 >> 16) & 0xFFFFu;
    UINT64 Ptr;
    int i;

    if (Xecp == 0) {
        return;
    }
    Ptr = gCapabilityBase + (UINT64)Xecp * 4u;
    for (i = 0; i < 64; i++) {
        UINT32 Val = Read32(Ptr);
        UINT8 Id = (UINT8)(Val & 0xFFu);
        UINT8 Next = (UINT8)((Val >> 8) & 0xFFu);
        if (Id == 1u) {
            Write32(Ptr, Val | (1u << 24));
            (void)RegisterWaitClear(Ptr, (1u << 16), 1000000u);
            return;
        }
        if (Next == 0) {
            break;
        }
        Ptr = gCapabilityBase + (UINT64)Next * 4u;
    }
}

static int ResetHc(void) {
    Write32(gOperationalBase, Read32(gOperationalBase) & ~USBCMD_RS);
    if (RegisterWaitSet(gOperationalBase + 4u, USBSTS_HCH, 1000000u) != 0) {
        return -1;
    }
    Write32(gOperationalBase, USBCMD_HCRST);
    if (RegisterWaitClear(gOperationalBase, USBCMD_HCRST, 1000000u) != 0) {
        return -1;
    }
    if (RegisterWaitClear(gOperationalBase + 4u, USBSTS_CNR, 1000000u) != 0) {
        return -1;
    }
    return 0;
}

static void *PagesAllocate(UINT32 N) {
    void *P = HalDmaAllocatePages(N);
    if (P) {
        MemoryZero(P, (UINTN)N * 4096u);
    }
    return P;
}

int ControllerStart(void) {
    UINT32 Cap;
    UINT32 CapLen;
    UINT32 Hcs1;
    UINT32 Hcs2;
    UINT32 Scratch;
    UINT32 Si;

    Cap = Read32(gCapabilityBase);
    CapLen = Cap & 0xFFu;
    if (CapLen < 0x20u || CapLen > 0x80u) {
        return -1;
    }
    gOperationalBase = gCapabilityBase + CapLen;
    gDoorbellBase = gCapabilityBase + (Read32(gCapabilityBase + 0x14u) & ~0x3u);
    gRuntimeBase = gCapabilityBase + (Read32(gCapabilityBase + 0x18u) & ~0x1Fu);
    gContextSize = (Read32(gCapabilityBase + 0x10u) & (1u << 2)) ? 64u : 32u;

    Hcs1 = Read32(gCapabilityBase + 0x04u);
    gMaxSlots = Hcs1 & 0xFFu;
    gMaxPorts = (Hcs1 >> 24) & 0xFFu;
    if (gMaxSlots == 0) {
        gMaxSlots = 1;
    }
    if (gMaxSlots > DEVICE_CONTEXT_SLOTS) {
        gMaxSlots = DEVICE_CONTEXT_SLOTS;
    }
    if (gMaxPorts == 0) {
        gMaxPorts = 1;
    }

    TakeLegacy();
    if (ResetHc() != 0) {
        return -1;
    }

    gDeviceContextBaseAddressArray = (UINT64 *)PagesAllocate(1);
    gCommandRing = (TRANSFER_REQUEST_BLOCK *)PagesAllocate(1);
    gEventRing = (TRANSFER_REQUEST_BLOCK *)PagesAllocate(1);
    gEventRingSegmentTable = (UINT8 *)PagesAllocate(1);
    gInputContext = (UINT8 *)PagesAllocate(1);
    gControlBuffer = (UINT8 *)PagesAllocate(1);
    gKeyboard.DeviceContext = (UINT8 *)PagesAllocate(1);
    gMouse.DeviceContext = (UINT8 *)PagesAllocate(1);
    gKeyboard.EndpointZeroRing = (TRANSFER_REQUEST_BLOCK *)PagesAllocate(1);
    gMouse.EndpointZeroRing = (TRANSFER_REQUEST_BLOCK *)PagesAllocate(1);
    gKeyboard.InterruptRing = (TRANSFER_REQUEST_BLOCK *)PagesAllocate(1);
    gMouse.InterruptRing = (TRANSFER_REQUEST_BLOCK *)PagesAllocate(1);
    if (!gDeviceContextBaseAddressArray || !gCommandRing || !gEventRing || !gEventRingSegmentTable || !gInputContext || !gControlBuffer ||
        !gKeyboard.DeviceContext || !gMouse.DeviceContext || !gKeyboard.EndpointZeroRing || !gMouse.EndpointZeroRing ||
        !gKeyboard.InterruptRing || !gMouse.InterruptRing) {
        return -1;
    }

    Hcs2 = Read32(gCapabilityBase + 0x08u);
    Scratch = (((Hcs2 >> 21) & 0x1Fu) << 5) | ((Hcs2 >> 27) & 0x1Fu);
    if (Scratch > 0) {
        UINT64 *Ptrs = (UINT64 *)PagesAllocate(1);
        if (Ptrs == 0) {
            return -1;
        }
        for (Si = 0; Si < Scratch && Si < 64u; Si++) {
            void *Page = PagesAllocate(1);
            if (Page == 0) {
                return -1;
            }
            Ptrs[Si] = PhysicalAddress(Page);
        }
        gDeviceContextBaseAddressArray[0] = PhysicalAddress(Ptrs);
    }

    Write32(gOperationalBase + 0x38u, gMaxSlots);
    Write64(gOperationalBase + 0x30u, PhysicalAddress(gDeviceContextBaseAddressArray));

    RingInitialize(gCommandRing, &gCommand, TRANSFER_RING_SIZE);
    Write64(gOperationalBase + 0x18u, PhysicalAddress(gCommandRing) | 1ULL);

    gEventDequeue = 0;
    gEventConsumerCycleState = 1;
    MemoryZero(gEventRingSegmentTable, 64);
    *(UINT64 *)(void *)gEventRingSegmentTable = PhysicalAddress(gEventRing);
    *(UINT16 *)(void *)(gEventRingSegmentTable + 8) = (UINT16)EVENT_RING_SIZE;
    Write32(gRuntimeBase + 0x20u, 3);
    Write32(gRuntimeBase + 0x24u, 0);
    Write32(gRuntimeBase + 0x28u, 1);
    Write32(gRuntimeBase + 0x2Cu, 0);
    Write64(gRuntimeBase + 0x30u, PhysicalAddress(gEventRingSegmentTable));
    Write64(gRuntimeBase + 0x38u, PhysicalAddress(gEventRing) | (1ULL << 3));

    MemoryFence();
    Write32(gOperationalBase, USBCMD_RS);
    MemoryFence();
    if (RegisterWaitClear(gOperationalBase + 4u, USBSTS_HCH, 1000000u) != 0) {
        return -1;
    }
    return 0;
}
