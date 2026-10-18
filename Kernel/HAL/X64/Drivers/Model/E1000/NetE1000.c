/*
 * NetE1000.c — e1000 / e1000e 经 Driver Net 类注册（PR-H4 / PR-N-nic-e1000）
 *
 * Probe 只认 PCI e1000*；Bind → NetAttachNicKind。
 * 无卡 → Probe 失败，不挡桌面。lsdev 名随芯片：e1000 / e1000e。
 */
#include "Driver.h"
#include "DriverBoot.h"
#include "DriverNic.h"
#include "E1000.h"
#include "E1000Private.h"
#include "Network.h"
#include "VirtualMemory.h"
#include "Hal.h"

static const DRIVER_MATCH gE1000Match[] = {
    { .Type = DRIVER_MATCH_PCI,
      .U.Pci = { .Vendor = E1000_VENDOR, .Device = E1000_DID_82540EM,
                 .VendorMask = 0xFFFF, .DeviceMask = 0xFFFF } },
    { .Type = DRIVER_MATCH_PCI,
      .U.Pci = { .Vendor = E1000_VENDOR, .Device = 0x100F,
                 .VendorMask = 0xFFFF, .DeviceMask = 0xFFFF } },
    { .Type = DRIVER_MATCH_PCI,
      .U.Pci = { .Vendor = E1000_VENDOR, .Device = E1000_DID_82574L,
                 .VendorMask = 0xFFFF, .DeviceMask = 0xFFFF } },
    { .Type = DRIVER_MATCH_PCI,
      .U.Pci = { .Vendor = E1000_VENDOR, .Device = 0x10F5,
                 .VendorMask = 0xFFFF, .DeviceMask = 0xFFFF } },
    { .Type = DRIVER_MATCH_PCI,
      .U.Pci = { .Vendor = E1000_VENDOR, .Device = E1000_DID_I219_LM,
                 .VendorMask = 0xFFFF, .DeviceMask = 0xFFFF } },
    { .Type = DRIVER_MATCH_NONE }
};

static char gE1000DriverName[8] = "e1000";

static void RefreshDriverName(void) {
    const char *Chip = E1000ChipName();
    int i;

    for (i = 0; i < 7 && Chip[i]; i++) {
        gE1000DriverName[i] = Chip[i];
    }
    gE1000DriverName[i] = 0;
}

static int E1000NicSendFrame(const UINT8 *Frame, UINTN FrameLen) {
    return E1000SendFrame(Frame, FrameLen);
}

static void E1000NicPoll(void) {
    E1000Poll();
}

static void E1000NicGetMac(UINT8 Mac[6]) {
    E1000GetMac(Mac);
}

static int E1000NicGetLink(int *Up, UINT32 *Mbps, int *FullDuplex) {
    return E1000GetLink(Up, Mbps, FullDuplex);
}

static const NIC_L2 gE1000NicL2 = {
    .SendFrame = E1000NicSendFrame,
    .Poll = E1000NicPoll,
    .GetMac = E1000NicGetMac,
    .GetLink = E1000NicGetLink,
};

static int E1000DriverProbe(const TOY_DRIVER *Self, void *BusCtx, void **OutPrivate) {
    const DEVICE_NODE *Dev = (const DEVICE_NODE *)BusCtx;

    (void)Self;
    if (!Dev || Dev->Bus != DEVICE_BUS_PCI || Dev->Bar[0] == 0) {
        return -1;
    }
    if (E1000Ready()) {
        RefreshDriverName();
        if (OutPrivate) {
            *OutPrivate = 0;
        }
        return 0;
    }
    if (!VirtualMemoryEnabled()) {
        return -1;
    }
    gPciDid = Dev->Device;
    gPciBus = Dev->PciBus;
    gPciDev = Dev->PciDev;
    gPciFn = Dev->PciFn;
    gBarPhys = Dev->Bar[0];
    if (!E1000Setup()) {
        return -1;
    }
    RefreshDriverName();
    if (OutPrivate) {
        *OutPrivate = 0;
    }
    return 0;
}

static int E1000DriverBind(TOY_DRIVER_INSTANCE *Inst) {
    int Up = 0;

    (void)Inst;
    if (!E1000Ready()) {
        return -1;
    }
    /*
     * 线没插就不占 L2。无线关联与网线无关；占着 down 的有线口时，
     * 托盘只能显示 down，DHCP 也跑在这张没有链路的卡上。
     */
    if (E1000GetLink(&Up, 0, 0) != 0 || !Up) {
        return 0;
    }
    return NetAttachNicKind(&gE1000NicL2, NET_NIC_KIND_WIRED);
}

static void E1000DriverRemove(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
}

static const TOY_DRIVER gE1000Driver = {
    .Name = gE1000DriverName,
    .Class = TOY_DRIVER_CLASS_NET,
    .Match = gE1000Match,
    .Probe = E1000DriverProbe,
    .Bind = E1000DriverBind,
    .Remove = E1000DriverRemove,
    .MaxInstances = 2,
};

void E1000DriverRegister(void) {
    (void)ToyDriverRegister(&gE1000Driver);
}

DRIVER_BOOT(DRIVER_BOOT_E1000, E1000DriverRegister);
