/*
 * Controller.c — TakeLegacy + 建环 + Run（QEMU）
 */
#include "Private.h"

static void TakeLegacy(void) {
    UINT32 Hcc1 = Rd32(gCap + 0x10u);
    UINT32 Xecp = (Hcc1 >> 16) & 0xFFFFu;
    UINT64 Ptr;
    int i;

    if (Xecp == 0) {
        return;
    }
    Ptr = gCap + (UINT64)Xecp * 4u;
    for (i = 0; i < 64; i++) {
        UINT32 Val = Rd32(Ptr);
        UINT8 Id = (UINT8)(Val & 0xFFu);
        UINT8 Next = (UINT8)((Val >> 8) & 0xFFu);
        if (Id == 1u) {
            Wr32(Ptr, Val | (1u << 24));
            (void)WaitClear(Ptr, (1u << 16), 1000000u);
            return;
        }
        if (Next == 0) {
            break;
        }
        Ptr = gCap + (UINT64)Next * 4u;
    }
}

static int ResetHc(void) {
    Wr32(gOp, Rd32(gOp) & ~USBCMD_RS);
    if (WaitSet(gOp + 4u, USBSTS_HCH, 1000000u) != 0) {
        return -1;
    }
    Wr32(gOp, USBCMD_HCRST);
    if (WaitClear(gOp, USBCMD_HCRST, 1000000u) != 0) {
        return -1;
    }
    if (WaitClear(gOp + 4u, USBSTS_CNR, 1000000u) != 0) {
        return -1;
    }
    return 0;
}

static void *AllocPages(UINT32 N) {
    void *P = HalDmaAllocatePages(N);
    if (P) {
        Zero(P, (UINTN)N * 4096u);
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

    Cap = Rd32(gCap);
    CapLen = Cap & 0xFFu;
    if (CapLen < 0x20u || CapLen > 0x80u) {
        return -1;
    }
    gOp = gCap + CapLen;
    gDb = gCap + (Rd32(gCap + 0x14u) & ~0x3u);
    gRt = gCap + (Rd32(gCap + 0x18u) & ~0x1Fu);
    gCtxSize = (Rd32(gCap + 0x10u) & (1u << 2)) ? 64u : 32u;

    Hcs1 = Rd32(gCap + 0x04u);
    gMaxSlots = Hcs1 & 0xFFu;
    gMaxPorts = (Hcs1 >> 24) & 0xFFu;
    if (gMaxSlots == 0) {
        gMaxSlots = 1;
    }
    if (gMaxSlots > DCBAA_SLOTS) {
        gMaxSlots = DCBAA_SLOTS;
    }
    if (gMaxPorts == 0) {
        gMaxPorts = 1;
    }

    TakeLegacy();
    if (ResetHc() != 0) {
        return -1;
    }

    gDcbaa = (UINT64 *)AllocPages(1);
    gCmdRing = (Trb *)AllocPages(1);
    gEvtRing = (Trb *)AllocPages(1);
    gErst = (UINT8 *)AllocPages(1);
    gInCtx = (UINT8 *)AllocPages(1);
    gCtrlBuf = (UINT8 *)AllocPages(1);
    gKbd.DevCtx = (UINT8 *)AllocPages(1);
    gMouse.DevCtx = (UINT8 *)AllocPages(1);
    gKbd.Ep0Ring = (Trb *)AllocPages(1);
    gMouse.Ep0Ring = (Trb *)AllocPages(1);
    gKbd.IntrRing = (Trb *)AllocPages(1);
    gMouse.IntrRing = (Trb *)AllocPages(1);
    if (!gDcbaa || !gCmdRing || !gEvtRing || !gErst || !gInCtx || !gCtrlBuf ||
        !gKbd.DevCtx || !gMouse.DevCtx || !gKbd.Ep0Ring || !gMouse.Ep0Ring ||
        !gKbd.IntrRing || !gMouse.IntrRing) {
        return -1;
    }

    Hcs2 = Rd32(gCap + 0x08u);
    Scratch = (((Hcs2 >> 21) & 0x1Fu) << 5) | ((Hcs2 >> 27) & 0x1Fu);
    if (Scratch > 0) {
        UINT64 *Ptrs = (UINT64 *)AllocPages(1);
        if (Ptrs == 0) {
            return -1;
        }
        for (Si = 0; Si < Scratch && Si < 64u; Si++) {
            void *Page = AllocPages(1);
            if (Page == 0) {
                return -1;
            }
            Ptrs[Si] = Phys(Page);
        }
        gDcbaa[0] = Phys(Ptrs);
    }

    Wr32(gOp + 0x38u, gMaxSlots);
    Wr64(gOp + 0x30u, Phys(gDcbaa));

    InitRing(gCmdRing, &gCmd, RING_SIZE);
    Wr64(gOp + 0x18u, Phys(gCmdRing) | 1ULL);

    gEvtDeq = 0;
    gEvtCcs = 1;
    Zero(gErst, 64);
    *(UINT64 *)(void *)gErst = Phys(gEvtRing);
    *(UINT16 *)(void *)(gErst + 8) = (UINT16)EVT_SIZE;
    Wr32(gRt + 0x20u, 3);
    Wr32(gRt + 0x24u, 0);
    Wr32(gRt + 0x28u, 1);
    Wr32(gRt + 0x2Cu, 0);
    Wr64(gRt + 0x30u, Phys(gErst));
    Wr64(gRt + 0x38u, Phys(gEvtRing) | (1ULL << 3));

    Fence();
    Wr32(gOp, USBCMD_RS);
    Fence();
    if (WaitClear(gOp + 4u, USBSTS_HCH, 1000000u) != 0) {
        return -1;
    }
    return 0;
}
