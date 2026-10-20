/*
 * InputEhci.c — ehci 经 Driver Input 类注册（PR-H-ehci-1/2）
 *
 * ehci-2：Bind → DriverInputAttach（mux 与 ps2/xhci 并存）。
 */
#include "Driver.h"
#include "DriverBoot.h"
#include "DriverInput.h"
#include "Ehci.h"
#include "VirtualMemory.h"

static void InputPoll(void) {
    if (Ready()) {
        HidPoll();
    }
}

static int KeyboardDequeue(HAL_KEYBOARD_REPORT *Report) {
    UINT8 Raw[8];
    int i;

    if (!Report || !HidReady()) {
        return 0;
    }
    if (!HidKeyboardDequeue(Raw)) {
        return 0;
    }
    Report->ModifierKeys = Raw[0];
    Report->Reserved = Raw[1];
    for (i = 0; i < 6; i++) {
        Report->KeyCode[i] = Raw[2 + i];
    }
    return 1;
}

static int MousePresentWrap(void) {
    return HidMousePresent();
}

static int MouseDequeue(HAL_MOUSE_REPORT *Report) {
    UINT32 X;
    UINT32 Y;
    UINT8 Btn;
    INT8 Wheel;

    if (!Report || !HidMousePresent()) {
        return 0;
    }
    if (!HidMouseDequeue(&X, &Y, &Btn, &Wheel)) {
        return 0;
    }
    Report->Buttons = Btn;
    Report->X = X;
    Report->Y = Y;
    Report->Wheel = Wheel;
    Report->Absolute = 0;
    return 1;
}

static const INPUT_BACKEND gEhciInputBackend = {
    .Poll = InputPoll,
    .KeyboardDequeue = KeyboardDequeue,
    .KeyboardSetLeds = 0,
    .MousePresent = MousePresentWrap,
    .MouseDequeue = MouseDequeue,
};

static const DRIVER_MATCH gEhciMatch[] = {
    { .Type = DRIVER_MATCH_PCI,
      .U.Pci = { .Class = 0x0C, .Subclass = 0x03, .ProgIf = 0x20,
                 .ClassMask = 0xFF, .SubclassMask = 0xFF, .ProgIfMask = 0xFF } },
    { .Type = DRIVER_MATCH_NONE }
};

static int DriverProbe(const DRIVER *Self, void *BusCtx, void **OutPrivate) {
    const DEVICE_NODE *Dev = (const DEVICE_NODE *)BusCtx;
    USB_CONTROLLER One;

    (void)Self;
    if (!Dev || Dev->Bus != DEVICE_BUS_PCI) {
        return -1;
    }
    if (Ready()) {
        if (OutPrivate) {
            *OutPrivate = 0;
        }
        return 0;
    }
    if (!VirtualMemoryEnabled()) {
        return -1;
    }
    if (!PciUsbAt(Dev->PciBus, Dev->PciDev, Dev->PciFn, &One) || !EhciClaim(&One)) {
        return -1;
    }
    if (!Setup()) {
        return -1;
    }
    (void)HidBringup(); /* 无 HID 仍占 lsdev；mux 空转 */
    if (OutPrivate) {
        *OutPrivate = 0;
    }
    return 0;
}

static int DriverBind(DRIVER_INSTANCE *Inst) {
    (void)Inst;
    if (!Ready()) {
        return -1;
    }
    return DriverInputAttach(&gEhciInputBackend);
}

static void DriverRemove(DRIVER_INSTANCE *Inst) {
    (void)Inst;
}

static const DRIVER gEhciDriver = {
    .Name = "ehci",
    .Class = DRIVER_CLASS_INPUT,
    .Match = gEhciMatch,
    .Probe = DriverProbe,
    .Bind = DriverBind,
    .Remove = DriverRemove,
};

void InputEhciRegister(void) {
    (void)DriverRegister(&gEhciDriver);
}

DRIVER_BOOT(DRIVER_BOOT_INPUT_EHCI, InputEhciRegister);
