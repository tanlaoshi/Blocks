/*
 * Block.c — NVMe 块设备经 Driver Block 类注册（PR-H5）
 *
 * 注册在 AHCI 之后；HalBlockInitialize 再 Probe 时若 NVMe 在场则覆盖 AHCI/ATA。
 */
#include "Block.h"
#include "DriverBoot.h"
#include "Driver.h"
#include "DriverBlock.h"
#include "Nvme.h"
#include "VirtualMemory.h"
#include "Hal.h"

static const BLOCK_BACKEND gNvmeBackend = {
    .Probe = NvmeProbe,
    .ReadSectors = NvmeReadSectors,
    .WriteSectors = NvmeWriteSectors,
    .Flush = 0,
};

static const DRIVER_MATCH gNvmeMatch[] = {
    {
        .Type = DRIVER_MATCH_PCI,
        .U.Pci = {
            .Class = 0x01,
            .Subclass = 0x08,
            .ProgIf = 0x02,
            .ClassMask = 0xFF,
            .SubclassMask = 0xFF,
            .ProgIfMask = 0xFF,
        },
    },
    { .Type = DRIVER_MATCH_NONE },
};

static int NvmeDriverProbe(const DRIVER *Self, void *BusCtx, void **OutPrivate) {
    DEVICE_NODE *Dev = (DEVICE_NODE *)BusCtx;

    (void)Self;
    if (!Dev || Dev->Bus != DEVICE_BUS_PCI || Dev->Bar[0] == 0) {
        return -1;
    }
    if (!VirtualMemoryEnabled()) {
        return -1;
    }
    if (!NvmeAccept(Dev->PciBus, Dev->PciDev, Dev->PciFn, Dev->Bar[0])) {
        return -1;
    }
    if (!NvmeSetup()) {
        return -1;
    }
    if (OutPrivate) {
        *OutPrivate = 0;
    }
    return 0;
}

static int NvmeDriverBind(DRIVER_INSTANCE *Inst) {
    (void)Inst;
    return DriverBlockAttach(&gNvmeBackend);
}

static void NvmeDriverRemove(DRIVER_INSTANCE *Inst) {
    (void)Inst;
}

static const DRIVER gNvmeDriver = {
    .Name = "nvme",
    .Class = DRIVER_CLASS_BLOCK,
    .Match = gNvmeMatch,
    .Probe = NvmeDriverProbe,
    .Bind = NvmeDriverBind,
    .Remove = NvmeDriverRemove,
    .MaxInstances = 2,
};

void NvmeDriverRegister(void) {
    (void)DriverRegister(&gNvmeDriver);
}

DRIVER_BOOT(DRIVER_BOOT_NVME, NvmeDriverRegister);
