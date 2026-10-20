/*
 * Hda.c — PR-G-audio-0：Intel HD Audio PCI 认卡（NUC7 / QEMU intel-hda）
 *
 * 只认卡 + lsdev；不映 BAR、不碰 CORB。无卡软退不挡桌面。
 */
#include "Hda.h"
#include "DriverBoot.h"
#include "Driver.h"
#include "Device.h"
#include "SerialLog.h"

static int gHdaProbed;
static UINT16 gHdaDid;
static UINT8 gHdaBus;
static UINT8 gHdaDev;
static UINT8 gHdaFn;
static DEVICE_NODE *gHdaNode;

int HdaProbed(void) {
    return gHdaProbed;
}

UINT16 HdaPciDid(void) {
    return gHdaDid;
}

UINT8 HdaPciBus(void) {
    return gHdaBus;
}

UINT8 HdaPciDev(void) {
    return gHdaDev;
}

UINT8 HdaPciFn(void) {
    return gHdaFn;
}

/* 8086 + Class 04 Subclass 03。不钉设备号。 */
static const DRIVER_MATCH gHdaMatch[] = {
    { .Type = DRIVER_MATCH_PCI,
      .U.Pci = { .Vendor = 0x8086, .VendorMask = 0xFFFF,
                 .Class = 0x04, .Subclass = 0x03,
                 .ClassMask = 0xFF, .SubclassMask = 0xFF } },
    { .Type = DRIVER_MATCH_NONE }
};

static int HdaProbe(const TOY_DRIVER *Self, void *BusCtx, void **OutPrivate) {
    DEVICE_NODE *Dev = (DEVICE_NODE *)BusCtx;

    (void)Self;
    if (OutPrivate) {
        *OutPrivate = 0;
    }
    if (!Dev || Dev->Bus != DEVICE_BUS_PCI) {
        LogBoot("Boot: hda skip (no Intel HDA)\n");
        return -1;
    }

    gHdaNode = Dev;
    gHdaDid = Dev->Device;
    gHdaBus = Dev->PciBus;
    gHdaDev = Dev->PciDev;
    gHdaFn = Dev->PciFn;
    gHdaProbed = 1;

    LogBoot("Boot: hda probe did=");
    LogBootHex32((UINT32)gHdaDid);
    LogBoot(" @");
    LogBootHex32((UINT32)gHdaBus);
    LogBoot(":");
    LogBootHex32((UINT32)gHdaDev);
    LogBoot(".");
    LogBootHex32((UINT32)gHdaFn);
    LogBoot("\n");
    return 0;
}

static int HdaBind(TOY_DRIVER_INSTANCE *Inst) {
    if (!Inst || !Inst->Driver || !gHdaNode) {
        return -1;
    }
    DeviceBindDriver(gHdaNode, Inst->Driver, Inst);
    LogBoot("Boot: hda bound\n");
    return 0;
}

static void HdaRemove(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
    gHdaProbed = 0;
    gHdaNode = 0;
    gHdaDid = 0;
}

static const TOY_DRIVER gHdaDriver = {
    .Name = "hda",
    .Class = TOY_DRIVER_CLASS_AUDIO,
    .Match = gHdaMatch,
    .Probe = HdaProbe,
    .Bind = HdaBind,
    .Remove = HdaRemove,
};

void HdaDriverRegister(void) {
    if (ToyDriverRegister(&gHdaDriver) != 0) {
        LogBoot("Boot: hda register fail\n");
        return;
    }
    (void)ToyDriverProbeClass(TOY_DRIVER_CLASS_AUDIO);
}

DRIVER_BOOT(DRIVER_BOOT_HDA, HdaDriverRegister);
