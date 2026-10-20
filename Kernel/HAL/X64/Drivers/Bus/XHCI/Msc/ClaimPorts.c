/*
 * ClaimPorts.c — PR-S-xhcimsc-1：根口/hub claim 编排。
 * Force/Address/Hub 帮手在 MscClaimPort.c。
 * MSC 全局仍定义在 Xhci.c（BSS 顺序影响 HID DMA 环；勿迁出）。
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

int ClaimPorts(void) {
    UINT32 P;
    int Ok = 0;
    int Sick = 0;

    if (!gXhciStarted || gOperationalBase == 0 || gMaxPorts == 0) {
        BootLog("Boot: MSC claim no hc\n");
        return -1;
    }
    if (gMscClaimed && gMscScanSlot != 0) {
        BootLogHex("Boot: MSC claim already port=", gMscPort, 2);
        return 1;
    }

    BootLog("Boot: MSC claim begin\n");
    HalBootLogoBump(80);
    /* Shell msc hot / 新一轮 claim：解开上次 sick 冻结 */
    gXhciPortEnumFreeze = 0;
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

    for (P = 1; P <= gMaxPorts && P <= 32u && !Sick; P++) {
        UINT32 Ps;
        UINT8 Speed;
        int Force;
        int HubRc;
        int Arc;

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
         * 真机跳过根口 FS/LS（几乎不是 MSC；软 Address 超时易 sick→鼠卡）。
         */
        if (!ForceUntilPed(P, &Force)) {
            continue;
        }
        if (Force == 0 && !HalCpuIsHypervisor()) {
            Speed = PortSpeed(Ps);
            if (Speed > 0 && Speed < 3) {
                continue;
            }
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
            goto done;
        }
    }

    if (!Ok && !Sick) {
        BootLog("Boot: MSC claim none\n");
    }
    if (Sick) {
        BootLog("Boot: MSC claim abort (cmd sick)\n");
    }

done:
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
    if (gSlotId != 0 && gIntrDci != 0) {
        QueueIntr();
    }
    if (gMouseSlotId != 0 && gMouseIntrDci != 0) {
        QueueMouseIntr();
    }
    return Ok;
}
