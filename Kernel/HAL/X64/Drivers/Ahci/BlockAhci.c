/*
 * BlockAhci.c — AHCI 块设备经 Driver Block 类注册（PR-H1）
 *
 * 注册顺序在 Ata 之前；HalBlockInitialize 在 VMM 后再 Probe，AHCI 绑定时
 * ToyDriverBlockAttach 覆盖 ATA（真机无 IDE 时 ATA 本就不会绑）。
 */
#include "Block.h"
#include "Driver.h"
#include "DriverBlock.h"
#include "Ahci.h"
#include "VirtualMemory.h"
#include "Hal.h"

static const BLOCK_BACKEND gAhciBackend = {
    .Probe = AhciProbe,
    .ReadSectors = AhciReadSectors,
    .WriteSectors = AhciWriteSectors,
    .Flush = 0,
};

static const DRIVER_MATCH gAhciMatch[] = {
    {
        .Type = DRIVER_MATCH_PCI,
        .U.Pci = {
            .Class = 0x01,
            .Subclass = 0x06,
            .ProgIf = 0x01,
            .ClassMask = 0xFF,
            .SubclassMask = 0xFF,
            .ProgIfMask = 0xFF,
        },
    },
    { .Type = DRIVER_MATCH_NONE },
};

static int AhciDriverProbe(const TOY_DRIVER *Self, void *BusCtx, void **OutPrivate) {
    DEVICE_NODE *Dev = (DEVICE_NODE *)BusCtx;

    (void)Self;
    if (!Dev || Dev->Bus != DEVICE_BUS_PCI || Dev->Bar[5] == 0) {
        return -1;
    }
    if (AhciReady()) {
        if (OutPrivate) {
            *OutPrivate = 0;
        }
        return 0;
    }
    /* HBA BAR 映射必须在 VMM Enable 之后（InitDriver 早 Probe 会跳过） */
    if (!VirtualMemoryEnabled()) {
        return -1;
    }
    if (!AhciAccept(Dev->PciBus, Dev->PciDev, Dev->PciFn, Dev->Bar[5])) {
        return -1;
    }
    if (!AhciSetup()) {
        return -1;
    }
    if (OutPrivate) {
        *OutPrivate = 0;
    }
    return 0;
}

static int AhciDriverBind(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
    return ToyDriverBlockAttach(&gAhciBackend);
}

static void AhciDriverRemove(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
}

static const TOY_DRIVER gAhciDriver = {
    .Name = "ahci",
    .Class = TOY_DRIVER_CLASS_BLOCK,
    .Match = gAhciMatch,
    .Probe = AhciDriverProbe,
    .Bind = AhciDriverBind,
    .Remove = AhciDriverRemove,
};

void AhciDriverRegister(void) {
    (void)ToyDriverRegister(&gAhciDriver);
}
