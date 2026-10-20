/*
 * BlockAta.c — x86 ATA PIO 块设备（PR-D2：经 Driver Block 类注册）
 */
#include "Block.h"
#include "DriverBoot.h"
#include "Driver.h"
#include "DriverBlock.h"
#include "Ata/Ata.h"
#include "Hal.h"

static const BLOCK_BACKEND gAtaBackend = {
    .Probe = AtaProbe,
    .ReadSectors = AtaReadSectors,
    .WriteSectors = AtaWriteSectors,
    .Flush = 0,
};

static int AtaDriverProbe(const DRIVER *Self, void *BusCtx, void **OutPrivate) {
    (void)Self;
    (void)BusCtx;
    /* 至少能 Probe 到 drive 0 才算有 ATA */
    if (!AtaProbe(0) && !AtaProbe(1)) {
        return -1;
    }
    if (OutPrivate) {
        *OutPrivate = 0;
    }
    return 0;
}

static int AtaDriverBind(DRIVER_INSTANCE *Inst) {
    (void)Inst;
    return DriverBlockAttach(&gAtaBackend);
}

static void AtaDriverRemove(DRIVER_INSTANCE *Inst) {
    (void)Inst;
    /* Block 后端全局一份；Remove 不拆（重启才清） */
}

static const DRIVER gAtaDriver = {
    .Name = "ata-pio",
    .Class = DRIVER_CLASS_BLOCK,
    .Match = 0,
    .Probe = AtaDriverProbe,
    .Bind = AtaDriverBind,
    .Remove = AtaDriverRemove,
};

void AtaDriverRegister(void) {
    (void)DriverRegister(&gAtaDriver);
}

int HalBlockInitialize(void) {
    /*
     * VMM 之后再 Probe Block 类：
     * - ATA 多在 InitDriver 已绑（PIO，无需 MMIO）
     * - AHCI（H1）/ NVMe（H5）需 MMIO，早 Probe 跳过，此处绑上并覆盖后端
     */
    (void)DriverProbeClass(DRIVER_CLASS_BLOCK);
    if (!BlockBackendReady()) {
        return 0;
    }
    return BlockInitialize();
}

DRIVER_BOOT(DRIVER_BOOT_ATA, AtaDriverRegister);
