/*
 * Nvme.c — NVMe 块设备（PR-H5 / 多 NS / MSI）
 *
 * Admin + 单 IO 队列、单页 PRP bounce。CQ 可 IEN=1 + PCI MSI；WaitCqe 仍 poll。
 * Identify 活动 NS 列表 → 各映 Block drive；4KiB LBA 在 Command 内 512e。
 * QEMU `-device nvme` / `DISK=nvme4k` / `DISK=nvmemulti`。
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
#include "SerialLog.h"

NVME_CTRL gControllers[NVME_MAX_CTRL];
int gControllerCount;
NVME_NAMESPACE gNamespaces[BLOCK_MAX_DRIVES];
int gNamespaceCount;
static int gReady;

int NvmeReady(void) {
    return gReady;
}

static UINT8 gPciBus;
static UINT8 gPciDev;
static UINT8 gPciFn;
static UINT64 gBar;
static int gHavePci;

static void LogHexDigit(UINT32 Value) {
    static const char Hex[] = "0123456789abcdef";
    char Digit[2];

    Digit[0] = Hex[Value & 0xf];
    Digit[1] = 0;
    LogFs(Digit);
}

static int NvmeRegisterNamespacesForController(NVME_CTRL *Controller) {
    UINT32 NamespaceIds[BLOCK_MAX_DRIVES];
    int Listed;
    int i;
    int Added = 0;

    Listed = NvmeFetchActiveNamespaceIds(Controller, NamespaceIds, BLOCK_MAX_DRIVES);
    for (i = 0; i < Listed && gNamespaceCount < BLOCK_MAX_DRIVES; i++) {
        UINT32 LbaShift = 9;
        NVME_NAMESPACE *Slot;

        if (!NvmeIdentifyNamespaceLbaShift(Controller, NamespaceIds[i], &LbaShift)) {
            continue;
        }
        Slot = &gNamespaces[gNamespaceCount];
        Slot->Controller = Controller;
        Slot->NamespaceId = NamespaceIds[i];
        Slot->LbaShift = LbaShift;
        Slot->Ready = 1;
        gNamespaceCount++;
        Added++;
        LogFs("nvme: NSID=");
        {
            static const char Hex[] = "0123456789abcdef";
            char Hex2[3];

            Hex2[0] = Hex[(NamespaceIds[i] >> 4) & 0xf];
            Hex2[1] = Hex[NamespaceIds[i] & 0xf];
            Hex2[2] = 0;
            LogFs(Hex2);
        }
        LogFs(" LBADS=");
        LogHexDigit(LbaShift);
        if (LbaShift != 9) {
            LogFs(" (512e translate)");
        }
        LogFs("\n");
    }
    return Added > 0 ? 1 : 0;
}

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
    NVME_CTRL *Controller;
    int Index;

    if (!VirtualMemoryEnabled() || !gHavePci || gBar == 0) {
        return 0;
    }
    for (Index = 0; Index < gControllerCount; Index++) {
        if (gControllers[Index].BarPhys == gBar) {
            return gControllers[Index].Ready ? 1 : 0;
        }
    }
    if (gControllerCount >= NVME_MAX_CTRL) {
        return 0;
    }

    Cmd = PciReadConfig(gPciBus, gPciDev, gPciFn, 0x04);
    PciWriteConfig(gPciBus, gPciDev, gPciFn, 0x04, Cmd | 0x06);

    Index = gControllerCount;
    Controller = &gControllers[Index];
    ZeroMemory(Controller, sizeof(*Controller));
    Controller->Bus = gPciBus;
    Controller->Device = gPciDev;
    Controller->Function = gPciFn;
    if (!CtrlInitialize(Controller, gBar)) {
        DebugWrite("nvme: ctrl init fail\n");
        return 0;
    }
    if (!NvmeRegisterNamespacesForController(Controller)) {
        DebugWrite("nvme: no namespace\n");
        Controller->Ready = 0;
        return 0;
    }
    gControllerCount = Index + 1;
    gReady = 1;
    LogFs("Boot: NVMe Drives=");
    LogHexDigit((UINT32)gNamespaceCount & 0xf);
    if (Controller->UseInterrupt) {
        LogFs(" IRQ=MSI");
    } else {
        LogFs(" IRQ=poll");
    }
    LogFs("\n");
    return 1;
}

int NvmeProbe(UINT32 Drive) {
    if (!gReady || Drive >= (UINT32)gNamespaceCount) {
        return 0;
    }
    return gNamespaces[Drive].Ready ? 1 : 0;
}

int NvmeReadSectors(UINT32 Drive, UINT32 Lba, UINT32 Count, void *Buffer) {
    if (!gReady || Drive >= (UINT32)gNamespaceCount) {
        return 0;
    }
    return NvmeTransfer(&gNamespaces[Drive], Lba, Count, Buffer, 0);
}

int NvmeWriteSectors(UINT32 Drive, UINT32 Lba, UINT32 Count, const void *Buffer) {
    if (!gReady || Drive >= (UINT32)gNamespaceCount) {
        return 0;
    }
    return NvmeTransfer(&gNamespaces[Drive], Lba, Count, (void *)Buffer, 1);
}
