/*
 * Enum.c — 根 hub 端口与 HID 接口探测
 *
 * 【初学者】
 * - UsbHid 子模块：找 boot keyboard / mouse 接口并建 slot。
 */
#include "Internal.h"

static UINT8 PortSpeed(UINT32 Portsc) {
    return (UINT8)((Portsc >> 10) & 0xFu);
}

static UINT16 SpeedMps(UINT8 Speed) {
    if (Speed >= 4u) {
        return 512;
    }
    if (Speed == 3u) {
        return 64;
    }
    return 8;
}

static int ResetPort(UINT32 Port1, UINT8 *SpeedOut) {
    UINT64 Ps = PortRegister(Port1);
    UINT32 Val = Read32(Ps);
    int i;

    if ((Val & PORTSC_PP) == 0) {
        Write32(Ps, (Val & ~PORTSC_CHANGE) | PORTSC_PP);
        (void)RegisterWaitSet(Ps, PORTSC_PP, 30000u);
        Val = Read32(Ps);
    }
    if ((Val & PORTSC_CCS) == 0) {
        for (i = 0; i < 50000; i++) {
            Val = Read32(Ps);
            if (Val & PORTSC_CCS) {
                break;
            }
        }
        if ((Val & PORTSC_CCS) == 0) {
            return -1;
        }
    }
    Val = PortStatusControlNeutral(Read32(Ps));
    Write32(Ps, Val | PORTSC_PR | PORTSC_PP);
    MemoryFence();
    if (RegisterWaitSet(Ps, PORTSC_PRC, 200000u) != 0) {
        return -1;
    }
    Val = Read32(Ps);
    if ((Val & PORTSC_PED) == 0) {
        (void)RegisterWaitSet(Ps, PORTSC_PED, 200000u);
        Val = Read32(Ps);
    }
    Write32(Ps, PortStatusControlNeutral(Val) | (Val & PORTSC_CHANGE) | PORTSC_PP);
    if ((Val & PORTSC_PED) == 0 || (Val & PORTSC_CCS) == 0) {
        return -1;
    }
    *SpeedOut = PortSpeed(Val);
    if (*SpeedOut == 0) {
        *SpeedOut = 3;
    }
    return 0;
}

static int AddressDevice(USB_HID_DEVICE *Device, UINT32 Port1, UINT8 Speed) {
    UINT32 Slot = 0;
    UINT32 *SlotCtx;
    UINT32 *EndpointZero;
    UINT64 Deq;

    gTransferDevice = Device;
    if (CommandSubmit(0, TRB_TYPE(TRB_ENABLE_SLOT), &Slot) < 0 || Slot == 0) {
        return -1;
    }
    Device->SlotIdentifier = Slot;
    Device->Port = Port1;
    Device->Speed = Speed;
    DeviceContextBaseAddressArraySet(Slot, PhysicalAddress(Device->DeviceContext));
    MemoryZero(Device->DeviceContext, CONTEXT_BYTES);
    MemoryZero(gInputContext, INPUT_CONTEXT_BYTES);
    *(UINT32 *)(void *)(gInputContext + 4) = (1u << 0) | (1u << 1);

    SlotCtx = (UINT32 *)(void *)InputContextSlot();
    SlotCtx[0] = (1u << 27) | ((UINT32)Speed << 20);
    SlotCtx[1] = (UINT32)Port1 << 16;

    RingInitialize(Device->EndpointZeroRing, &Device->EndpointZero, TRANSFER_RING_SIZE);
    EndpointZero = (UINT32 *)(void *)InputContextEndpoint(1);
    EndpointZero[1] = (3u << 1) | (4u << 3) | ((UINT32)SpeedMps(Speed) << 16);
    Deq = PhysicalAddress(Device->EndpointZeroRing) | 1ULL;
    EndpointZero[2] = (UINT32)Deq;
    EndpointZero[3] = (UINT32)(Deq >> 32);
    EndpointZero[4] = 8;

    if (CommandSubmit(PhysicalAddress(gInputContext), TRB_TYPE(TRB_ADDRESS_DEV) | TRB_SLOT(Slot), 0) < 0) {
        Device->SlotIdentifier = 0;
        return -1;
    }
    return 0;
}

static void DisableSlot(USB_HID_DEVICE *Device) {
    if (Device->SlotIdentifier == 0) {
        return;
    }
    (void)CommandSubmit(0, TRB_TYPE(10u) | TRB_SLOT(Device->SlotIdentifier), 0); /* DISABLE_SLOT */
    DeviceContextBaseAddressArraySet(Device->SlotIdentifier, 0);
    Device->SlotIdentifier = 0;
    Device->InterruptDeviceContextIndex = 0;
}

static int TryRole(UINT32 Port1, USB_HID_DEVICE *Device, int WantKeyboard) {
    UINT8 Speed = 0;
    volatile int Delay;

    if (ResetPort(Port1, &Speed) != 0) {
        return -1;
    }
    for (Delay = 0; Delay < 200000; Delay++) {
    }
    if (AddressDevice(Device, Port1, Speed) != 0) {
        return -1;
    }
    if (DeviceConfigure(Device, WantKeyboard) != 0) {
        DisableSlot(Device);
        return -1;
    }
    return 0;
}

static int ClaimPort(UINT32 Port1) {
    if (gKeyboardOk && gMouseOk) {
        return 0;
    }
    if (!gKeyboardOk && TryRole(Port1, &gKeyboard, 1) == 0) {
        gKeyboardOk = 1;
        return 0;
    }
    if (!gMouseOk && TryRole(Port1, &gMouse, 0) == 0) {
        gMouseOk = 1;
        return 0;
    }
    return -1;
}

int EnumerateAndBind(void) {
    UINT32 P;
    int Pass;

    for (Pass = 0; Pass < 3; Pass++) {
        for (P = 1; P <= gMaxPorts && P <= 32u; P++) {
            if ((Read32(PortRegister(P)) & PORTSC_CCS) == 0) {
                continue;
            }
            (void)ClaimPort(P);
            if (gKeyboardOk && gMouseOk) {
                return 0;
            }
        }
    }
    return (gKeyboardOk || gMouseOk) ? 0 : -1;
}
