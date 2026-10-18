/*
 * Nvme.c — 最小 NVMe 读写（PR-H5）
 *
 * 同步轮询、Admin + 单 IO 队列、512B LBA、单页 PRP bounce；无中断。
 * QEMU `-device nvme` / 真机 PCIe NVMe；FAT/VFS 不动。
 */

#include "Nvme.h"
#include "Private.h"
#include "Block.h"
#include "PCIe.h"
#include "PhysicalMemory.h"
#include "VirtualMemory.h"
#include "Debug.h"
#include "Hal.h"
#include "HalSerial.h"
#include "ToySerialLog.h"

NVME_CTRL gCtrl[NVME_MAX_CTRL];
int gCtrlCount;
static int gReady;

int NvmeReady(void) {
    return gReady;
}

static UINT8 gPciBus;
static UINT8 gPciDev;
static UINT8 gPciFn;
static UINT64 gBar;
static int gHavePci;

int NvmeAccept(UINT8 Bus, UINT8 Dev, UINT8 Fn, UINT64 Bar) {
    if (Bar == 0) {
        return 0;
    }
    gPciBus = Bus;
    gPciDev = Dev;
    gPciFn = Fn;
    gBar = Bar;
    gHavePci = 1;
    return 1;
}

int NvmeSetup(void) {
    UINT32 Cmd;
    NVME_CTRL *C;
    int Index;

    if (!VirtualMemoryEnabled() || !gHavePci || gBar == 0) {
        return 0;
    }
    for (Index = 0; Index < gCtrlCount; Index++) {
        if (gCtrl[Index].BarPhys == gBar) {
            return gCtrl[Index].Ready ? 1 : 0;
        }
    }
    if (gCtrlCount >= 2) {
        return 0;
    }

    Cmd = PciReadConfig(gPciBus, gPciDev, gPciFn, 0x04);
    PciWriteConfig(gPciBus, gPciDev, gPciFn, 0x04, Cmd | 0x06);

    Index = gCtrlCount;
    C = &gCtrl[Index];
    ZeroMemory(C, sizeof(*C));
    if (!CtrlInit(C, gBar)) {
        DebugWrite("nvme: ctrl init fail\n");
        return 0;
    }
    DebugWrite("nvme: ctrl ");
    DebugHex32((UINT32)Index);
    DebugWrite(" bar=");
    DebugHex32((UINT32)gBar);
    DebugWrite("\n");
    gCtrlCount = Index + 1;

    if (Index != 0) {
        return 1;
    }

    gReady = 1;
    ToyLogFs("Boot: NVMe Drives=");
    {
        static const char Hex[] = "0123456789abcdef";
        char Buf[2];
        Buf[0] = Hex[gCtrlCount & 0xf];
        Buf[1] = 0;
        ToyLogFs(Buf);
    }
    ToyLogFs("\n");
    return 1;
}

int NvmeProbe(UINT32 Drive) {
    if (!gReady || Drive >= (UINT32)gCtrlCount) {
        return 0;
    }
    return gCtrl[Drive].Ready ? 1 : 0;
}

int NvmeReadSectors(UINT32 Drive, UINT32 Lba, UINT32 Count, void *Buffer) {
    if (!gReady || Drive >= (UINT32)gCtrlCount) {
        return 0;
    }
    return Xfer(&gCtrl[Drive], Lba, Count, Buffer, 0);
}

int NvmeWriteSectors(UINT32 Drive, UINT32 Lba, UINT32 Count, const void *Buffer) {
    if (!gReady || Drive >= (UINT32)gCtrlCount) {
        return 0;
    }
    return Xfer(&gCtrl[Drive], Lba, Count, (void *)Buffer, 1);
}
