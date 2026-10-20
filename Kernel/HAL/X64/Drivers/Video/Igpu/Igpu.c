/*
 * Igpu.c — PR-G-igpu-0：Intel VGA/Display PCI 认卡（NUC7 UHD 620）
 *
 * 只认卡 + lsdev；不映 BAR、不提交命令。无卡（QEMU）软退不挡桌面。
 */
#include "Igpu.h"
#include "DriverBoot.h"
#include "Driver.h"
#include "Device.h"
#include "SerialLog.h"

static int gIgpuProbed;
static UINT16 gIgpuDid;
static UINT8 gIgpuBus;
static UINT8 gIgpuDev;
static UINT8 gIgpuFn;
static DEVICE_NODE *gIgpuNode;

int IgpuProbed(void) {
    return gIgpuProbed;
}

UINT16 IgpuPciDid(void) {
    return gIgpuDid;
}

UINT8 IgpuPciBus(void) {
    return gIgpuBus;
}

UINT8 IgpuPciDev(void) {
    return gIgpuDev;
}

UINT8 IgpuPciFn(void) {
    return gIgpuFn;
}

/*
 * 8086 + Class 03。Subclass 00 是 VGA，80 是其它显示控制器。
 * 不钉设备号。
 */
static const DRIVER_MATCH gIgpuMatch[] = {
    { .Type = DRIVER_MATCH_PCI,
      .U.Pci = { .Vendor = 0x8086, .VendorMask = 0xFFFF,
                 .Class = 0x03, .Subclass = 0x00,
                 .ClassMask = 0xFF, .SubclassMask = 0xFF } },
    { .Type = DRIVER_MATCH_PCI,
      .U.Pci = { .Vendor = 0x8086, .VendorMask = 0xFFFF,
                 .Class = 0x03, .Subclass = 0x80,
                 .ClassMask = 0xFF, .SubclassMask = 0xFF } },
    { .Type = DRIVER_MATCH_NONE }
};

static int IgpuProbe(const TOY_DRIVER *Self, void *BusCtx, void **OutPrivate) {
    DEVICE_NODE *Dev = (DEVICE_NODE *)BusCtx;

    (void)Self;
    if (OutPrivate) {
        *OutPrivate = 0;
    }
    if (!Dev || Dev->Bus != DEVICE_BUS_PCI) {
        LogBoot("Boot: igpu skip (no Intel display)\n");
        return -1;
    }

    gIgpuNode = Dev;
    gIgpuDid = Dev->Device;
    gIgpuBus = Dev->PciBus;
    gIgpuDev = Dev->PciDev;
    gIgpuFn = Dev->PciFn;
    gIgpuProbed = 1;

    LogBoot("Boot: igpu probe did=");
    LogBootHex32((UINT32)gIgpuDid);
    LogBoot(" @");
    LogBootHex32((UINT32)gIgpuBus);
    LogBoot(":");
    LogBootHex32((UINT32)gIgpuDev);
    LogBoot(".");
    LogBootHex32((UINT32)gIgpuFn);
    LogBoot("\n");
    return 0;
}

static int IgpuBind(TOY_DRIVER_INSTANCE *Inst) {
    if (!Inst || !Inst->Driver || !gIgpuNode) {
        return -1;
    }
    DeviceBindDriver(gIgpuNode, Inst->Driver, Inst);
    LogBoot("Boot: igpu bound\n");
    return 0;
}

static void IgpuRemove(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
    gIgpuProbed = 0;
    gIgpuNode = 0;
    gIgpuDid = 0;
}

static const TOY_DRIVER gIgpuDriver = {
    .Name = "igpu",
    .Class = TOY_DRIVER_CLASS_DISPLAY,
    .Match = gIgpuMatch,
    .Probe = IgpuProbe,
    .Bind = IgpuBind,
    .Remove = IgpuRemove,
};

void IgpuDriverRegister(void) {
    if (ToyDriverRegister(&gIgpuDriver) != 0) {
        LogBoot("Boot: igpu register fail\n");
        return;
    }
    (void)ToyDriverProbeClass(TOY_DRIVER_CLASS_DISPLAY);
}

DRIVER_BOOT(DRIVER_BOOT_IGPU, IgpuDriverRegister);
