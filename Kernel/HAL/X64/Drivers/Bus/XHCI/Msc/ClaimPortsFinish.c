/*
 * ClaimPortsFinish.c — ClaimPorts 收口（sick Recover / freeze / 鼠口）（自 ClaimPorts.c 纯搬 · PR-NORM-11）
 *
 * sick：Recover + 重武装 HID，保持 Dual MSI；勿 FallbackToPoll。
 * 无 sick 扫空：只 freeze，勿再 QueueIntr（usb-hid-keep）。
 */
#include "XHCI/Private.h"

/* 根口鼠标被扫盘复位后，槽号仍在会挡住补绑。 */
void ClaimPortsNoteMouseRootDropped(void) {
    UINT32 Ps;

    if (gMouseSlotId == 0 || gMousePort == 0 || gMouseHubSlot != 0) {
        return;
    }
    Ps = ReadMmio32(gOperationalBase + PortRegister(gMousePort));
    if ((Ps & PORTSC_CCS) && (Ps & PORTSC_PED)) {
        return;
    }
    BootLog("Boot: MSC root dropped mouse\n");
    DisableSlot(gMouseSlotId);
}

int ClaimPortsFinishAfterScan(int Ok, int Sick) {
    /* watch 钉/清见 XhciMscBootWatchAfterClaim；EnsureSecondary 勿对 freeze Hot */
    XhciMscBootWatchAfterClaim(Ok);
    /*
     * sick：Recover + 重武装 HID，保持 Dual MSI。
     * 勿 FallbackToPoll——切 POLL 后鼠标轮询极卡（U 盘启 Claim 失败常见）。
     * 返回 -1：AutoBeforeFs 勿再整轮重试。
     */
    if (gXhciCmdSick || Sick) {
        BootLog("Boot: MSC claim recover after sick\n");
        RecoverCommandRing();
        gXhciCmdSick = 0;
        gXhciCmdWaitMs = 0;
        gXhciCmdAttempts = 0;
        /* 冻结后续 UART/WiFi 等根口 Force，避免再超时把鼠打卡 */
        gXhciPortEnumerateFreeze = 1;
        BootLog("Boot: XHCI port enum freeze (keep mouse)\n");
        if (!HalCpuIsHypervisor()) {
            int i;

            for (i = 0; i < 32; i++) {
                ProcessEventsRealPc();
            }
            ServiceHidCompletions();
        }
        ClaimPortsNoteMouseRootDropped();
        /* 勿 RecoverEndpoint0：sick 后再发 Stop/ResetEP 会二次超时（见最新串口） */
        if (gSlotId != 0 && gIntrDci != 0) {
            QueueIntr();
        }
        if (gMouseSlotId != 0 && gMouseIntrDci != 0) {
            QueueMouseIntr();
        }
        return -1;
    }
    ClaimPortsNoteMouseRootDropped();
    /*
     * usb-hid-keep：无 sick 时键鼠 IN 仍挂着（桌面 secondary / 扫空常见）。
     * 再 QueueIntr/QueueMouseIntr = 双投递 → HC 不完成 → 鼠死、键只能偶发。
     * sick 路径上方已 Recover 后重武装；此处只 freeze，勿碰 HID 环。
     */
    if (!Ok) {
        gXhciPortEnumerateFreeze = 1;
        BootLog("Boot: XHCI port enum freeze (msc empty, keep mouse)\n");
    }
    return Ok;
}
