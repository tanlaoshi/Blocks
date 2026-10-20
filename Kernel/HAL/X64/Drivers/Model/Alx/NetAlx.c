/*
 * NetAlx.c — alx 经 Driver Net 类注册（PR-N-alx-2）
 *
 * Probe 认 PCI 1969:1091…；Bind → NetAttachNicKind。
 */
#include "Driver.h"
#include "DriverBoot.h"
#include "DriverNic.h"
#include "Alx.h"
#include "AlxPrivate.h"
#include "Network/Network.h"
#include "VirtualMemory.h"

static const DRIVER_MATCH gAlxMatch[] = {
    { .Type = DRIVER_MATCH_PCI,
      .U.Pci = { .Vendor = ALX_VENDOR, .Device = ALX_DID_AR8161,
                 .VendorMask = 0xFFFF, .DeviceMask = 0xFFFF } },
    { .Type = DRIVER_MATCH_PCI,
      .U.Pci = { .Vendor = ALX_VENDOR, .Device = ALX_DID_AR8162,
                 .VendorMask = 0xFFFF, .DeviceMask = 0xFFFF } },
    { .Type = DRIVER_MATCH_PCI,
      .U.Pci = { .Vendor = ALX_VENDOR, .Device = ALX_DID_AR8171,
                 .VendorMask = 0xFFFF, .DeviceMask = 0xFFFF } },
    { .Type = DRIVER_MATCH_PCI,
      .U.Pci = { .Vendor = ALX_VENDOR, .Device = ALX_DID_AR8172,
                 .VendorMask = 0xFFFF, .DeviceMask = 0xFFFF } },
    { .Type = DRIVER_MATCH_NONE }
};

static int AlxNicSendFrame(const UINT8 *Frame, UINTN FrameLen) {
    return AlxSendFrame(Frame, FrameLen);
}

static void AlxNicPoll(void) {
    AlxPoll();
}

static void AlxNicGetMac(UINT8 Mac[6]) {
    AlxGetMac(Mac);
}

static int AlxNicGetLink(int *Up, UINT32 *Mbps, int *FullDuplex) {
    return AlxGetLink(Up, Mbps, FullDuplex);
}

static const NIC_L2 gAlxNicL2 = {
    .SendFrame = AlxNicSendFrame,
    .Poll = AlxNicPoll,
    .GetMac = AlxNicGetMac,
    .GetLink = AlxNicGetLink,
};

static int AlxDriverProbe(const TOY_DRIVER *Self, void *BusCtx, void **OutPrivate) {
    const DEVICE_NODE *Dev = (const DEVICE_NODE *)BusCtx;

    (void)Self;
    if (!Dev || Dev->Bus != DEVICE_BUS_PCI || Dev->Bar[0] == 0) {
        return -1;
    }
    if (AlxReady()) {
        if (OutPrivate) {
            *OutPrivate = 0;
        }
        return 0;
    }
    if (!VirtualMemoryEnabled()) {
        return -1;
    }
    if (!AlxAccept(Dev->PciBus, Dev->PciDev, Dev->PciFn, Dev->Bar[0], Dev->Device)) {
        return -1;
    }
    if (!AlxSetup()) {
        return -1;
    }
    if (OutPrivate) {
        *OutPrivate = 0;
    }
    return 0;
}

static int AlxDriverBind(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
    if (!AlxReady()) {
        return -1;
    }
    return NetAttachNicKind(&gAlxNicL2, NET_NIC_KIND_WIRED);
}

static void AlxDriverRemove(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
}

static const TOY_DRIVER gAlxDriver = {
    .Name = "alx",
    .Class = TOY_DRIVER_CLASS_NET,
    .Match = gAlxMatch,
    .Probe = AlxDriverProbe,
    .Bind = AlxDriverBind,
    .Remove = AlxDriverRemove,
};

void AlxDriverRegister(void) {
    (void)ToyDriverRegister(&gAlxDriver);
}

DRIVER_BOOT(DRIVER_BOOT_ALX, AlxDriverRegister);
