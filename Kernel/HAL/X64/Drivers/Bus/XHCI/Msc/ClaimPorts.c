/*
 * ClaimPorts.c — 根口/hub MSC claim 编排（usb-map / usb-hid-keep / PR-S-xhcimsc-1）
 *
 * ```
 * UsbMscClaim|AutoBeforeFs|Hot → ClaimPorts
 *   入口：若 freeze 则直接 return 0（Hot 须先 XhciPortEnumerateUnfreeze）
 *   若 cmd sick：Recover + QueueIntr/QueueMouseIntr（保持 Dual MSI，勿 FallbackToPoll）
 *   → HubEnumerate | ClaimPort(ForceUntilPed→Address) → FinishClaim
 *   跳过：kbd/mouse/hub 根口、已认 FTDI/CDC 口
 * 收口：
 *   Ok → Ready；成功不 freeze；勿重 Queue HID
 *   扫空 → freeze=1；勿重 Queue HID（IN 仍挂着）
 *   sick → freeze=1 + Recover + 重武装 HID
 * ```
 * 保鼠要点：WaitCommand 内仍 ServiceHidCompletions；sick 后勿 RecoverEndpoint0；
 * 无 sick 收口勿再 QueueIntr/QueueMouseIntr（secondary 扫空双投递会打死挂起的 IN）。
 * AddressDeviceOnPort/ForceUntilPed 见 freeze 则拒。Wifi/FTDI/CDC claim 入口亦早退。
 * Force/Address：ClaimPort.c；全局 BSS：Xhci.c。
 * 根口扫描 / 收口：ClaimPortsScan.c / ClaimPortsFinish.c（PR-NORM-11）。
 */
#include "XHCI/Private.h"

void XhciPortEnumerateUnfreeze(void) {
    gXhciPortEnumerateFreeze = 0;
}

int XhciPortEnumerateIsFrozen(void) {
    return gXhciPortEnumerateFreeze ? 1 : 0;
}

int ClaimPorts(void) {
    int Ok = 0;
    int Sick = 0;

    if (!gXhciStarted || gOperationalBase == 0 || gMaxPorts == 0) {
        BootLog("Boot: MSC claim no hc\n");
        return -1;
    }
    /*
     * 扫空/sick 后 freeze：勿在此清零。Worker HotPoll~5s 若每次解开再 Address，
     * 桌面期持续抢命令环 → 空拖假死。Shell msc hot 先 Unfreeze。
     */
    if (gXhciPortEnumerateFreeze) {
        return 0;
    }
    if (gMscClaimed && gMscScanSlot != 0) {
        BootLogHex("Boot: MSC claim already port=", gMscPort, 2);
        return 1;
    }

    BootLog("Boot: MSC claim begin\n");
    HalBootLogoBump(80);
    /* 根口 CCS 明细仅 VERBOSE（默认可读：begin → ok/none） */
    if (DiagVerbose()) {
        UINT32 Sp;

        for (Sp = 1; Sp <= gMaxPorts && Sp <= 32u; Sp++) {
            UINT32 Ps = ReadMmio32(gOperationalBase + PortRegister(Sp));

            if (!(Ps & PORTSC_CCS)) {
                continue;
            }
            BootLogHex("Boot: MSC claim survey ccs port=", Sp, 2);
            BootLogHex("Boot: MSC claim survey portsc=", Ps, 8);
            BootLogHex("Boot: MSC claim survey spd=", PortSpeed(Ps), 1);
        }
    }
    /*
     * excl-3：不再 FallbackToPoll("msc-claim")。
     * 事件环由 excl-1/2 单消费者 + 消费锁串行；claim 保持 dual/irq。
     */
    /*
     * 12:40 成功：Force !PED 0x05 → EnableSlot（可需 Recover 重试）→ hub → MSC。
     * soft-fail 会留下挂起 TRB → irq-stall；恢复为正常 CA+重试，并重武装 HID。
     */
    if (gXhciCmdSick) {
        BootLog("Boot: MSC claim recover cmd sick\n");
        RecoverCommandRing();
        gXhciCmdSick = 0;
        if (gSlotId != 0 && gIntrDci != 0) {
            QueueIntr();
        }
        if (gMouseSlotId != 0 && gMouseIntrDci != 0) {
            QueueMouseIntr();
        }
    }
    (void)BringUp();

    if (gMscScanSlot != 0) {
        DisableSlot(gMscScanSlot);
        gMscScanSlot = 0;
    }
    gMscClaimed = 0;
    gMscPort = 0;
    gMscRoute = 0;
    gMscHubSlot = 0;
    gMscTtPort = 0;
    gMscCapacityOk = 0;
    gMscBlockCount = 0;
    gMscBlockSize = 0;

    /* 键盘/鼠已走 hub：先扫子口找 MSC（与 U 盘同 hub 时） */
    if (gHubSlotId != 0 && EnumerateHubChildren()) {
        Ok = 1;
        return ClaimPortsFinishAfterScan(Ok, Sick);
    }

    /*
     * Pass0：软 Address（冷启动 U 盘常驻 PED，先 Force 易挂）。
     * Pass1：仅扫 Pass0 记入 NeedForce 的口再 Force——U 盘启动后固件留下的
     * PED 口软 Address 常失败，旧逻辑本轮不回头 → claim none → Files 无 U 盘。
     */
    if (ClaimPortsScanRootPorts(&Ok, &Sick)) {
        return ClaimPortsFinishAfterScan(Ok, Sick);
    }

    if (!Ok && !Sick) {
        BootLog("Boot: MSC claim none\n");
    }
    if (Sick) {
        BootLog("Boot: MSC claim abort (cmd sick)\n");
    }
    return ClaimPortsFinishAfterScan(Ok, Sick);
}
