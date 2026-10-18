/*
 * Input.c — uhci 经 Driver 注册（PR-H-uhci-1：仅 Probe/CCS）
 */
#include "Driver.h"
#include "DriverBoot.h"
#include "Uhci.h"
#include "PCIe.h"
#include "UsbHostController.h"

void UhciHostControllerOffer(void);

static const DRIVER_MATCH gUhciMatch[] = {
    { .Type = DRIVER_MATCH_PCI,
      .U.Pci = { .Class = 0x0C, .Subclass = 0x03, .ProgIf = 0x00,
                 .ClassMask = 0xFF, .SubclassMask = 0xFF, .ProgIfMask = 0xFF } },
    { .Type = DRIVER_MATCH_NONE }
};

static int UhciDriverProbe(const TOY_DRIVER *Self, void *BusCtx, void **OutPrivate) {
    const DEVICE_NODE *Dev = (const DEVICE_NODE *)BusCtx;
    USB_CONTROLLER One;

    (void)Self;
    if (!Dev || Dev->Bus != DEVICE_BUS_PCI) {
        return -1;
    }
    if (UhciReady()) {
        if (OutPrivate) {
            *OutPrivate = 0;
        }
        return 0;
    }
    if (!PciUsbAt(Dev->PciBus, Dev->PciDev, Dev->PciFn, &One) || !UhciClaim(&One)) {
        return -1;
    }
    if (!UhciSetup()) {
        return -1;
    }
    UhciHostControllerOffer();
    if (OutPrivate) {
        *OutPrivate = 0;
    }
    return 0;
}

static int UhciDriverBind(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
    return UhciReady() ? 0 : -1;
}

static void UhciDriverRemove(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
}

static const TOY_DRIVER gUhciDriver = {
    .Name = "uhci",
    .Class = TOY_DRIVER_CLASS_INPUT,
    .Match = gUhciMatch,
    .Probe = UhciDriverProbe,
    .Bind = UhciDriverBind,
    .Remove = UhciDriverRemove,
};

void InputUhciRegister(void) {
    (void)ToyDriverRegister(&gUhciDriver);
}

DRIVER_BOOT(DRIVER_BOOT_INPUT_UHCI, InputUhciRegister);
