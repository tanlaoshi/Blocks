/*
 * IrqEnable.c — EnableIrq（自 Irq.c 纯搬 · PR-NORM-11）
 *
 * QEMU：MSI/IOAPIC → DUAL。真机：试 dual；失败 → poll (fallback)，Drain 永不关。
 */
#include "XHCI/Private.h"
#include "HalIrq.h"
#include "PCIe.h"


/* PHOTO/Shell：mode=poll|dual|irq + t/i/k/m/u/s/c/r/d/q（q=IRQ 进入次数） */

/* Arm 后打一枪：期望的键鼠 slot/DCI，便于对照 s=. */

/*
 * QEMU：MSI/IOAPIC → DUAL。真机：试 dual；失败 → poll (fallback)，Drain 永不关。
 */
int EnableIrq(USB_CONTROLLER *Device) {
    UINT8 Dest;

    if (gUseGetReport || (gSlotId == 0 && gMouseSlotId == 0)) {
        DebugWrite("XHCI: no interrupt EP, IRQ unused\n");
        gUseIrq = 0;
        gIrqMode = XHCI_IRQ_MODE_POLL;
        LogUsb("Boot: XHCI IRQ=None\n");
        return 0;
    }

    /* PHOTO / show xhci：Arm 后计数清零 */
    gStatIntrEvt = 0;
    gStatMouseEvt = 0;
    gStatKeyboardPush = 0;
    gStatMousePush = 0;
    gStatTransferAny = 0;
    gStatUnmatched = 0;
    gStatEvtRing = 0;
    gStatDrain = 0;
    gStatLastCc = 0;
    gStatLastSlot = 0;
    gStatLastEndpoint = 0;
    gStatIrq = 0;
    gStatIrqSkipped = 0;
    gDiagTransferLogged = 0;
    gDiagIntrCcLogged = 0;
    DiagLogArms();

    /*
     * 真机 Arm：只 Queue，勿 Sync（Stop+SetDeq 曾弄死 kbd / PHOTO r=0）。
     */
    if (gSlotId != 0 && gIntrDci != 0) {
        QueueIntr();
    }
    if (gMouseSlotId != 0 && gMouseIntrDci != 0) {
        QueueMouseIntr();
    }
    /* Queue 产生的 Stopped 勿计入 PHOTO */
    gStatIntrEvt = 0;
    gStatMouseEvt = 0;
    gStatKeyboardPush = 0;
    gStatMousePush = 0;
    gStatTransferAny = 0;
    gStatUnmatched = 0;
    gStatEvtRing = 0;
    gStatDrain = 0;
    gStatLastCc = 0;
    gStatLastSlot = 0;
    gStatLastEndpoint = 0;

    HalIrqNotifyRegister(VEC_XHCI, Irq);

    if (TryEnterDual(Device)) {
        DrainEvents();
        return 1;
    }

    /* QEMU：再试 INTx→IOAPIC；真机 dual 失败则纯 poll backup */
    if (HalCpuIsHypervisor()) {
        Dest = HalCpuApicId(InputIrqCpu());
        if (PciEnableIoApicIntx(Device, VEC_XHCI, Dest)) {
            EnableHostInterrupts();
            gUseIrq = 0;
            DrainEvents();
            gUseIrq = 1;
            gIrqMode = XHCI_IRQ_MODE_DUAL;
            LogUsb("Boot: XHCI IRQ=IOAPIC (Dual)\n");
            return 1;
        }
    }

    DebugWrite("XHCI: MSI failed; poll drain backup\n");
    FallbackToPoll("no-msi");
    return 0;
}

/* 返回是否武装了设备中断（DUAL/IRQ）；POLL 时仍靠 Drain */
