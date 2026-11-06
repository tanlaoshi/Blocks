/*
 * Interrupt.c — NVMe PCI MSI / MSI-X（CQ IEN=1；WaitCqe 仍 poll）
 *
 * 对齐 E1000：PciEnableMsi + HalIrqNotifyRegister；IRQ 只计数，不抢 CQ 头。
 */
#include "Nvme.h"
#include "Private.h"
#include "PCIe.h"
#include "Hal.h"
#include "HalIrq.h"
#include "HalPort.h"
#include "SerialLog.h"

static volatile UINT32 gInterruptCount;
static int gNotifyRegistered;

static void FillPciBars(USB_CONTROLLER *Device) {
    int Index;

    for (Index = 0; Index < 6;) {
        UINT32 Low = PciReadConfig(Device->Bus, Device->Device, Device->Function,
                                   (UINT8)(0x10 + Index * 4));
        UINT64 Bar;
        UINT32 Type;

        if (Low & 1u) {
            Device->Bar[Index] = Low & ~0x3u;
            Index++;
            continue;
        }
        Bar = Low & 0xFFFFFFF0ULL;
        Type = (Low >> 1) & 3u;
        if (Type == 2u && Index + 1 < 6) {
            UINT32 High = PciReadConfig(Device->Bus, Device->Device, Device->Function,
                                        (UINT8)(0x10 + (Index + 1) * 4));
            Bar |= ((UINT64)High) << 32;
            Device->Bar[Index] = Bar;
            Device->Bar[Index + 1] = 0;
            Index += 2;
        } else {
            Device->Bar[Index] = Bar;
            Index++;
        }
    }
}

void NvmeInterrupt(void) {
    gInterruptCount++;
}

int NvmeArmMessageSignaledInterrupt(NVME_CTRL *Controller) {
    USB_CONTROLLER Device;

    if (!Controller || !Controller->Bar) {
        return 0;
    }

    ZeroMemory(&Device, sizeof(Device));
    Device.Bus = Controller->Bus;
    Device.Device = Controller->Device;
    Device.Function = Controller->Function;
    FillPciBars(&Device);
    if (Device.Bar[0] == 0) {
        Device.Bar[0] = Controller->BarPhys;
    }

    if (!gNotifyRegistered) {
        HalIrqNotifyRegister(VEC_NVME, NvmeInterrupt);
        gNotifyRegistered = 1;
    }
    if (!PciEnableMsi(&Device, (UINT8)VEC_NVME, 0)) {
        LogFs("nvme: MSI failed; stay poll\n");
        return 0;
    }

    /* 解屏蔽全部向量（单 MSI / MSI-X entry0 → IV=0） */
    MmioW32(Controller->Bar, NVME_REG_INTMC, 0xFFFFFFFFu);
    Controller->UseInterrupt = 1;
    return 1;
}
