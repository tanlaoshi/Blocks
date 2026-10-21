/*
 * ClaimPorts.c — 根口/hub MSC claim 编排（usb-map / usb-hid-keep / PR-S-xhcimsc-1）
 *
 * ```
 * UsbMscClaim|AutoBeforeFs|Hot → ClaimPorts
 *   入口：若 freeze 则直接 return 0（Hot 须先 XhciPortEnumUnfreeze）
 *   若 cmd sick：Recover + QueueIntr/QueueMouseIntr（保持 Dual MSI，勿 FallbackToPoll）
 *   → HubEnumerate | ClaimPort(ForceUntilPed→Address) → FinishClaim
 *   跳过：kbd/mouse/hub 根口、已认 FTDI/CDC 口
 * 收口：
 *   Ok → Ready；成功不 freeze；勿重 Queue HID
 *   扫空 → freeze=1；勿重 Queue HID（IN 仍挂着）
 *   sick → freeze=1 + Recover + 重武装 HID
 * ```
 * 保鼠要点：WaitCommand 内仍 ServiceHidCompletions；sick 后勿 RecoverEp0；
 * 无 sick 收口勿再 QueueIntr/QueueMouseIntr（secondary 扫空双投递会打死挂起的 IN）。
 * AddressDeviceOnPort/ForceUntilPed 见 freeze 则拒。Wifi/FTDI/CDC claim 入口亦早退。
 * Force/Address：ClaimPort.c；全局 BSS：Xhci.c。
 */
#include "XHCI/Private.h"

/*
 * PR-H-msc-4：单口 claim — Address（可 Force PR）+ SetConfig + Bulk IN/OUT。
 * 优先扫已有 hub 子口（键鼠经 hub 时 U 盘常在同 hub）；再扫其它根口。
 * 不 SCSI、不挂 FAT、不碰键鼠口。成功则保留 slot；Ready=1。
 */
/* 根口鼠标被扫盘复位后，槽号仍在会挡住补绑。 */
static void NoteMouseRootDropped(void) {
    UINT32 Ps;

    if (gMouseSlotId == 0 || gMousePort == 0 || gMouseHubSlot != 0) {
        return;
    }
    Ps = ReadMmio32(gOperationalBase + PortReg(gMousePort));
    if ((Ps & PORTSC_CCS) && (Ps & PORTSC_PED)) {
        return;
    }
    BootLog("Boot: MSC root dropped mouse\n");
    DisableSlot(gMouseSlotId);
}

void XhciPortEnumUnfreeze(void) {
    gXhciPortEnumFreeze = 0;
}

int XhciPortEnumIsFrozen(void) {
    return gXhciPortEnumFreeze ? 1 : 0;
}

int ClaimPorts(void) {
    UINT32 P;
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
    if (gXhciPortEnumFreeze) {
        return 0;
    }
    if (gMscClaimed && gMscScanSlot != 0) {
        BootLogHex("Boot: MSC claim already port=", gMscPort, 2);
        return 1;
    }

    BootLog("Boot: MSC claim begin\n");
    HalBootLogoBump(80);
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
        goto done;
    }

    /*
     * Pass0：软 Address（冷启动 U 盘常驻 PED，先 Force 易挂）。
     * Pass1：仅扫 Pass0 记入 NeedForce 的口再 Force——U 盘启动后固件留下的
     * PED 口软 Address 常失败，旧逻辑本轮不回头 → claim none → Files 无 U 盘。
     */
    {
        int Pass;

        for (Pass = 0; Pass < 2 && !Ok && !Sick; Pass++) {
            if (Pass == 1) {
                if (gPortNeedForcePr == 0) {
                    break;
                }
                BootLog("Boot: MSC claim NeedForce pass\n");
            }
            for (P = 1; P <= gMaxPorts && P <= 32u && !Sick; P++) {
                UINT32 Ps;
                UINT8 Speed;
                int Force;
                int HubRc;
                int Arc;

                if (Pass == 1 && !(gPortNeedForcePr & (1u << P))) {
                    continue;
                }
                Ps = ReadMmio32(gOperationalBase + PortReg(P));
                if (!(Ps & PORTSC_CCS)) {
                    continue;
                }
                HalBootLogoBump(90);
                if (gSlotId != 0 && P == gPort1) {
                    continue;
                }
                if (gMouseSlotId != 0 && P == gMousePort) {
                    continue;
                }
                if (gHubSlotId != 0 && P == gHubRootPort) {
                    continue;
                }
                if (gFtdiClaimed && gFtdiPort == P) {
                    continue;
                }
                if (gCdcClaimed && gCdcPort == P) {
                    continue;
                }

                /*
                 * 已 PED：软 Address；未 PED / NeedForce：Force。
                 * 真机根口 FS/LS 不碰（几乎不是 U 盘；Force PR 会弄掉邻口鼠 — NUC
                 * RUNTIME：口8 dclass=0xE0 反复 NeedForce → root dropped mouse + cc=12）。
                 */
                Speed = PortSpeed(Ps);
                if (!HalCpuIsHypervisor() && Speed > 0 && Speed < 3) {
                    gPortNeedForcePr &= ~(1u << P);
                    continue;
                }
                if (!ForceUntilPed(P, &Force)) {
                    continue;
                }
                /*
                 * U 盘起但 claim 常失败（PED 软 Address cc=04）：记下口，
                 * PollUnplug 盯 CCS——棒拔了仍能钉 Gone 出大窗。
                 */
                if (gMscBootWatchArm) {
                    gMscBootWatchPort = P;
                }

                Arc = AddressPort(P, Force, &Speed);
                if (Arc < 0) {
                    Sick = 1;
                    break;
                }
                if (Arc == 0) {
                    continue;
                }

                gXferSlot = gMscScanSlot;
                if (GetDeviceDesc() < 0) {
                    BootLogHex("Boot: MSC claim desc fail port=", P, 2);
                    gPortNeedForcePr |= (1u << P);
                    DisableSlot(gMscScanSlot);
                    gMscScanSlot = 0;
                    continue;
                }

                HubRc = TryHubOnRoot(P, Speed, &Ok);
                if (HubRc == 1) {
                    goto done;
                }
                if (HubRc == 0) {
                    if (gXhciCmdSick) {
                        Sick = 1;
                        break;
                    }
                    continue;
                }

                if (FinishClaim(P, Speed)) {
                    Ok = 1;
                    gMscBootWatchPort = 0;
                    gMscBootWatchArm = 0;
                    goto done;
                }
            }
        }
    }

    if (!Ok && !Sick) {
        BootLog("Boot: MSC claim none\n");
    }
    if (Sick) {
        BootLog("Boot: MSC claim abort (cmd sick)\n");
    }
    if (!Ok && gMscBootWatchArm && gMscBootWatchPort != 0) {
        BootLogHex("Boot: MSC boot-watch port=", gMscBootWatchPort, 2);
    }

done:
    if (Ok) {
        gMscBootWatchPort = 0;
        gMscBootWatchArm = 0;
    }
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
        gXhciPortEnumFreeze = 1;
        BootLog("Boot: XHCI port enum freeze (keep mouse)\n");
        if (!HalCpuIsHypervisor()) {
            int i;

            for (i = 0; i < 32; i++) {
                ProcessEventsRealPc();
            }
            ServiceHidCompletions();
        }
        NoteMouseRootDropped();
        /* 勿 RecoverEp0：sick 后再发 Stop/ResetEP 会二次超时（见最新串口） */
        if (gSlotId != 0 && gIntrDci != 0) {
            QueueIntr();
        }
        if (gMouseSlotId != 0 && gMouseIntrDci != 0) {
            QueueMouseIntr();
        }
        return -1;
    }
    NoteMouseRootDropped();
    /*
     * usb-hid-keep：无 sick 时键鼠 IN 仍挂着（桌面 secondary / 扫空常见）。
     * 再 QueueIntr/QueueMouseIntr = 双投递 → HC 不完成 → 鼠死、键只能偶发。
     * sick 路径上方已 Recover 后重武装；此处只 freeze，勿碰 HID 环。
     */
    if (!Ok) {
        gXhciPortEnumFreeze = 1;
        BootLog("Boot: XHCI port enum freeze (msc empty, keep mouse)\n");
    }
    return Ok;
}
