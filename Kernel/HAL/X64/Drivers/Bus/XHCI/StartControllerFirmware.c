/*
 * StartControllerFirmware.c — 真机固件 DCBAAP + 私有命令/事件环（自 Controller.c 纯搬 · PR-NORM-11）
 *
 * 真机：固件 DCBAAP/scratch 保留（自建 DCBAAP 曾致 RS 挂）；
 * 命令环+事件环用私有（固件事件环无法可靠吃到 EnableSlot 完成）。
 */
#include "XHCI/Private.h"

int StartControllerWithFirmwareDcbaap(UINT32 MaxSlots) {
    UINT64 FwDcbaap;
    UINTN MapBytes;
    UINT32 Slot;

    FwDcbaap = gFwDcbaapSave ? gFwDcbaapSave
                             : (ReadMmio64(gOperationalBase + 0x30) & ~0x3FULL);
    if (FwDcbaap == 0) {
        BootMarkUsb("Boot: XHCI fw DCBAAP=0\n");
        return 0;
    }
    MapBytes = (UINTN)(MaxSlots + 1) * sizeof(UINT64);
    if (MapBytes < XHCI_PAGE_SIZE) {
        MapBytes = XHCI_PAGE_SIZE;
    }
    if (MapXhciDma(FwDcbaap, MapBytes) != 0) {
        BootMarkUsb("Boot: XHCI map DCBAAP fail\n");
        return 0;
    }
    gDcbaaLive = (UINT64 *)(UINTN)FwDcbaap;
    gDcbaaFromFirmware = 1;
    WriteMmio32(gOperationalBase + 0x38, MaxSlots);
    for (Slot = 1; Slot <= MaxSlots; Slot++) {
        gDcbaaLive[Slot] = 0;
    }
    DcbaaFlush();
    WriteMmio64(gOperationalBase + 0x30, FwDcbaap);
    BootMarkV("Boot: XHCI use fw DCBAAP\n");

    /*
     * 真机：DCBAAP 必须固件（否则 RS 挂）；命令/事件环改私有。
     */
    (void)gFwCrcrSave;
    (void)gFwErstbaSave;
    (void)gFwEvtSave;
    (void)gFwErdpSave;
    gCmdRingLive = gCmdRing;
    gEvtRingLive = gEvtRing;
    gEvtRingSize = EVT_SIZE;
    RingInitialize(gCmdRing, &gCmd, RING_SIZE);
    FlushDma(gCmdRing, sizeof(gCmdRing));
    (void)MapXhciDma(PointerToPhysical(gCmdRing), sizeof(gCmdRing));
    WriteMmio64(gOperationalBase + 0x18, PointerToPhysical(gCmdRing) | 1ULL);

    ZeroMemory(gEvtRing, sizeof(gEvtRing));
    gEvtDeq = 0;
    gEvtCcs = 1;
    ZeroMemory(gErst, sizeof(gErst));
    *(UINT64 *)(void *)gErst = PointerToPhysical(gEvtRing);
    *(UINT16 *)(void *)(gErst + 8) = (UINT16)EVT_SIZE;
    FlushDma(gEvtRing, sizeof(gEvtRing));
    FlushDma(gErst, sizeof(gErst));
    (void)MapXhciDma(PointerToPhysical(gEvtRing), sizeof(gEvtRing));
    (void)MapXhciDma(PointerToPhysical(gErst), sizeof(gErst));
    WriteMmio32(gRuntimeBase + 0x20, 0);
    WriteMmio32(gRuntimeBase + 0x24, 0);
    WriteMmio32(gRuntimeBase + 0x28, 1);
    WriteMmio32(gRuntimeBase + 0x2C, 0);
    WriteMmio64(gRuntimeBase + 0x30, PointerToPhysical(gErst));
    WriteMmio64(gRuntimeBase + 0x38, PointerToPhysical(gEvtRing) | (1ULL << 3));

    Fence();
    BootMarkRs('b', 'R');
    WriteMmio32(gOperationalBase, USBCMD_RS);
    Fence();
    BootMarkRs('a', 'R');
    if (!WaitClear(gOperationalBase + 4, USBSTS_HCH, 100000)) {
        DiagChk("StartController.fwRS", 0, "HCH=0", ReadMmio32(gOperationalBase + 4), 8);
        BootLog("Boot: XHCI run timeout\n");
        return 0;
    }
    if (!WaitSet(gOperationalBase + 0x18, CRCR_CRR, 100000)) {
        if (DiagVerbose()) {
            DiagChk("StartController.CRR", 0, "CRR=1", ReadMmio32(gOperationalBase + 0x18), 8);
            BootMarkUsb("Boot: XHCI CRR TO\n");
        }
    } else {
        DiagChk("StartController.fwRS", 1, "HCH=0+CRR", ReadMmio32(gOperationalBase + 4), 8);
    }
    BootMarkV("Boot: XHCI RS running\n");
    return 1;
}
