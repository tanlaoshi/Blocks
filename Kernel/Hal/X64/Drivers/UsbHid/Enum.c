/*
 * Enum.c — 根口复位 / Address / HID 认领 / 中断 EP
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
    UINT64 Ps = PortReg(Port1);
    UINT32 Val = Rd32(Ps);
    int i;

    if ((Val & PORTSC_PP) == 0) {
        Wr32(Ps, (Val & ~PORTSC_CHANGE) | PORTSC_PP);
        (void)WaitSet(Ps, PORTSC_PP, 30000u);
        Val = Rd32(Ps);
    }
    if ((Val & PORTSC_CCS) == 0) {
        for (i = 0; i < 50000; i++) {
            Val = Rd32(Ps);
            if (Val & PORTSC_CCS) {
                break;
            }
        }
        if ((Val & PORTSC_CCS) == 0) {
            return -1;
        }
    }
    Val = PortscNeutral(Rd32(Ps));
    Wr32(Ps, Val | PORTSC_PR | PORTSC_PP);
    Fence();
    if (WaitSet(Ps, PORTSC_PRC, 200000u) != 0) {
        return -1;
    }
    Val = Rd32(Ps);
    if ((Val & PORTSC_PED) == 0) {
        (void)WaitSet(Ps, PORTSC_PED, 200000u);
        Val = Rd32(Ps);
    }
    Wr32(Ps, PortscNeutral(Val) | (Val & PORTSC_CHANGE) | PORTSC_PP);
    if ((Val & PORTSC_PED) == 0 || (Val & PORTSC_CCS) == 0) {
        return -1;
    }
    *SpeedOut = PortSpeed(Val);
    if (*SpeedOut == 0) {
        *SpeedOut = 3;
    }
    return 0;
}

static int AddressDevice(USB_HID_DEVICE *D, UINT32 Port1, UINT8 Speed) {
    UINT32 Slot = 0;
    UINT32 *SlotCtx;
    UINT32 *Ep0;
    UINT64 Deq;

    gTransferDevice = D;
    if (Command(0, TRB_TYPE(TRB_ENABLE_SLOT), &Slot) < 0 || Slot == 0) {
        return -1;
    }
    D->SlotId = Slot;
    D->Port = Port1;
    D->Speed = Speed;
    DcbaaSet(Slot, Phys(D->DevCtx));
    Zero(D->DevCtx, CTX_BYTES);
    Zero(gInCtx, INCTX_BYTES);
    *(UINT32 *)(void *)(gInCtx + 4) = (1u << 0) | (1u << 1);

    SlotCtx = (UINT32 *)(void *)InSlot();
    SlotCtx[0] = (1u << 27) | ((UINT32)Speed << 20);
    SlotCtx[1] = (UINT32)Port1 << 16;

    InitRing(D->Ep0Ring, &D->Ep0, RING_SIZE);
    Ep0 = (UINT32 *)(void *)InEp(1);
    Ep0[1] = (3u << 1) | (4u << 3) | ((UINT32)SpeedMps(Speed) << 16);
    Deq = Phys(D->Ep0Ring) | 1ULL;
    Ep0[2] = (UINT32)Deq;
    Ep0[3] = (UINT32)(Deq >> 32);
    Ep0[4] = 8;

    if (Command(Phys(gInCtx), TRB_TYPE(TRB_ADDRESS_DEV) | TRB_SLOT(Slot), 0) < 0) {
        D->SlotId = 0;
        return -1;
    }
    return 0;
}

static void DisableSlot(USB_HID_DEVICE *D) {
    if (D->SlotId == 0) {
        return;
    }
    (void)Command(0, TRB_TYPE(10u) | TRB_SLOT(D->SlotId), 0); /* DISABLE_SLOT */
    DcbaaSet(D->SlotId, 0);
    D->SlotId = 0;
    D->IntrDci = 0;
}

static int TryRole(UINT32 Port1, USB_HID_DEVICE *D, int WantKbd) {
    UINT8 Speed = 0;
    volatile int Delay;

    if (ResetPort(Port1, &Speed) != 0) {
        return -1;
    }
    for (Delay = 0; Delay < 200000; Delay++) {
    }
    if (AddressDevice(D, Port1, Speed) != 0) {
        return -1;
    }
    if (Finish(D, WantKbd) != 0) {
        DisableSlot(D);
        return -1;
    }
    return 0;
}

static int ClaimPort(UINT32 Port1) {
    if (gKbdOk && gMouseOk) {
        return 0;
    }
    if (!gKbdOk && TryRole(Port1, &gKbd, 1) == 0) {
        gKbdOk = 1;
        return 0;
    }
    if (!gMouseOk && TryRole(Port1, &gMouse, 0) == 0) {
        gMouseOk = 1;
        return 0;
    }
    return -1;
}

int EnumAndBind(void) {
    UINT32 P;
    int Pass;

    for (Pass = 0; Pass < 3; Pass++) {
        for (P = 1; P <= gMaxPorts && P <= 32u; P++) {
            if ((Rd32(PortReg(P)) & PORTSC_CCS) == 0) {
                continue;
            }
            (void)ClaimPort(P);
            if (gKbdOk && gMouseOk) {
                return 0;
            }
        }
    }
    return (gKbdOk || gMouseOk) ? 0 : -1;
}
