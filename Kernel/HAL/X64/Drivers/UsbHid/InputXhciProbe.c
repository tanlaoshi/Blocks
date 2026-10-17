/*
 * InputXhciProbe.c — xHCI 这一个 PCI 功能的 Probe（PR-S3-inputxhci-1）
 */
#include "Driver.h"
#include "Hal.h"
#include "ToySerialLog.h"
#include "PCIe.h"
#include "UsbHid/Private.h"
#include "Debug.h"
#include "VirtualMemory.h"
#include "UsbHid/InputXhciPrivate.h"

static int TryFallback(void **OutPrivate) {
    UINT64 Fallback = HalPlatformXhciFallback();

    /* 真机勿二次 Init（同 BAR 再 reset 会挂） */
    if (Fallback == 0 || !HalCpuIsHypervisor()) {
        return -1;
    }
    gXhciDev.Bus = 0;
    gXhciDev.Device = 0;
    gXhciDev.Function = 0;
    gXhciDev.BaseAddress = Fallback;
    gXhciDev.Bar[0] = Fallback;
    gXhciDev.Type = 0x30;
    if (TryXhciAt(Fallback, &gXhciDev) &&
        (HidKeyboardReady() || MousePresent())) {
        gXhciReady = 1;
        ToyLogUsb("Boot: XHCI-HID Keyboard\n");
        if (OutPrivate) {
            *OutPrivate = 0;
        }
        return 0;
    }
    ToyLogUsb("Boot: XHCI fallback BAR failed / no HID\n");
    return -1;
}

int DriverProbe(const TOY_DRIVER *Self, void *BusCtx, void **OutPrivate) {
    const DEVICE_NODE *Dev = (const DEVICE_NODE *)BusCtx;
    USB_CONTROLLER One;

    (void)Self;
    if (gXhciReady) {
        if (OutPrivate) {
            *OutPrivate = 0;
        }
        return 0;
    }
    /* xHCI MMIO 需在 VMM Enable 之后映射 */
    if (!VirtualMemoryEnabled()) {
        return -1;
    }

    if (!Dev || Dev->Bus != DEVICE_BUS_PCI ||
        !PciUsbAt(Dev->PciBus, Dev->PciDev, Dev->PciFn, &One) ||
        One.Type != 0x30) {
        return TryFallback(OutPrivate);
    }

    gXhciDev = One;
    DebugWrite("XHCI: pci ");
    DebugHex32(One.Bus);
    DebugWrite(":");
    DebugHex32(One.Device);
    DebugWrite(".");
    DebugHex32(One.Function);
    DebugWrite("\n");
    if (TryXhciAt(One.BaseAddress, &gXhciDev)) {
        if (HidKeyboardReady() || MousePresent()) {
            gXhciReady = 1;
            if (!HidKeyboardReady() && MousePresent()) {
                ToyLogUsb("Boot: XHCI-HID Mouse-Only Bind\n");
            }
            if (OutPrivate) {
                *OutPrivate = 0;
            }
            return 0;
        }
        ToyLogUsb("Boot: XHCI up (no HID), try PS/2\n");
        return TryFallback(OutPrivate);
    }
    ToyLogUsb("Boot: XHCI init failed at BAR\n");
    return TryFallback(OutPrivate);
}
