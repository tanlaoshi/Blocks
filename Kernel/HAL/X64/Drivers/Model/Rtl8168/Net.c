/*
 * Net.c — r8169 经 Driver Net 类注册（PR-N-rtl-2）
 *
 * Probe 认 PCI 10EC:8168…；Bind → NetAttachNicKind。
 */
#include "Driver.h"
#include "DriverBoot.h"
#include "DriverNic.h"
#include "Rtl8168.h"
#include "Private.h"
#include "Network/Network.h"
#include "VirtualMemory.h"

static const DRIVER_MATCH gRtlMatch[] = {
    { .Type = DRIVER_MATCH_PCI,
      .U.Pci = { .Vendor = RTL_VENDOR, .Device = RTL_DID_8168,
                 .VendorMask = 0xFFFF, .DeviceMask = 0xFFFF } },
    { .Type = DRIVER_MATCH_PCI,
      .U.Pci = { .Vendor = RTL_VENDOR, .Device = RTL_DID_8161,
                 .VendorMask = 0xFFFF, .DeviceMask = 0xFFFF } },
    { .Type = DRIVER_MATCH_PCI,
      .U.Pci = { .Vendor = RTL_VENDOR, .Device = RTL_DID_8162,
                 .VendorMask = 0xFFFF, .DeviceMask = 0xFFFF } },
    { .Type = DRIVER_MATCH_PCI,
      .U.Pci = { .Vendor = RTL_VENDOR, .Device = RTL_DID_8167,
                 .VendorMask = 0xFFFF, .DeviceMask = 0xFFFF } },
    { .Type = DRIVER_MATCH_PCI,
      .U.Pci = { .Vendor = RTL_VENDOR, .Device = RTL_DID_8169,
                 .VendorMask = 0xFFFF, .DeviceMask = 0xFFFF } },
    { .Type = DRIVER_MATCH_PCI,
      .U.Pci = { .Vendor = RTL_VENDOR, .Device = RTL_DID_8136,
                 .VendorMask = 0xFFFF, .DeviceMask = 0xFFFF } },
    { .Type = DRIVER_MATCH_NONE }
};

static int RtlNicSendFrame(const UINT8 *Frame, UINTN FrameLen) {
    return RtlSendFrame(Frame, FrameLen);
}

static void RtlNicPoll(void) {
    RtlPoll();
}

static void RtlNicGetMac(UINT8 Mac[6]) {
    RtlGetMac(Mac);
}

static int RtlNicGetLink(int *Up, UINT32 *Mbps, int *FullDuplex) {
    return RtlGetLink(Up, Mbps, FullDuplex);
}

static const NIC_L2 gRtlNicL2 = {
    .SendFrame = RtlNicSendFrame,
    .Poll = RtlNicPoll,
    .GetMac = RtlNicGetMac,
    .GetLink = RtlNicGetLink,
};

static int RtlDriverProbe(const DRIVER *Self, void *BusCtx, void **OutPrivate) {
    const DEVICE_NODE *Dev = (const DEVICE_NODE *)BusCtx;

    (void)Self;
    if (!Dev || Dev->Bus != DEVICE_BUS_PCI || Dev->Bar[0] == 0) {
        return -1;
    }
    if (RtlReady()) {
        if (OutPrivate) {
            *OutPrivate = 0;
        }
        return 0;
    }
    if (!VirtualMemoryEnabled()) {
        return -1;
    }
    if (!RtlAccept(Dev->PciBus, Dev->PciDev, Dev->PciFn, Dev->Bar[0], Dev->Device)) {
        return -1;
    }
    if (!RtlSetup()) {
        return -1;
    }
    if (OutPrivate) {
        *OutPrivate = 0;
    }
    return 0;
}

static int RtlDriverBind(DRIVER_INSTANCE *Inst) {
    (void)Inst;
    if (!RtlReady()) {
        return -1;
    }
    return NetAttachNicKind(&gRtlNicL2, NET_NIC_KIND_WIRED);
}

static void RtlDriverRemove(DRIVER_INSTANCE *Inst) {
    (void)Inst;
}

static const DRIVER gRtlDriver = {
    .Name = "r8169",
    .Class = DRIVER_CLASS_NET,
    .Match = gRtlMatch,
    .Probe = RtlDriverProbe,
    .Bind = RtlDriverBind,
    .Remove = RtlDriverRemove,
};

void RtlDriverRegister(void) {
    (void)DriverRegister(&gRtlDriver);
}

DRIVER_BOOT(DRIVER_BOOT_RTL8168, RtlDriverRegister);
