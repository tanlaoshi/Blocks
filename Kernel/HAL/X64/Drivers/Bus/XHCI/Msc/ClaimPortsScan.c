/*
 * ClaimPortsScan.c — ClaimPorts 根口 Pass0/Pass1 扫描（自 ClaimPorts.c 纯搬 · PR-NORM-11）
 *
 * Pass0：软 Address；Pass1：仅 NeedForce 口再 Force。
 * 跳过键鼠/hub/FTDI/CDC/BT 根口；真机根口 FS/LS 不碰。
 */
#include "XHCI/Private.h"

int ClaimPortsScanRootPorts(int *Ok, int *Sick) {
    UINT32 P;
    int Pass;

    for (Pass = 0; Pass < 2 && !*Ok && !*Sick; Pass++) {
        if (Pass == 1) {
            if (gPortNeedForcePr == 0) {
                break;
            }
            BootLog("Boot: MSC claim NeedForce pass\n");
        }
        for (P = 1; P <= gMaxPorts && P <= 32u && !*Sick; P++) {
            UINT32 Ps;
            UINT8 Speed;
            int Force;
            int HubRc;
            int Arc;

            if (Pass == 1 && !(gPortNeedForcePr & (1u << P))) {
                continue;
            }
            Ps = ReadMmio32(gOperationalBase + PortRegister(P));
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
            /* btx-1 已软探的 HCI 口：勿再 Force/Address（曾把 boot-watch 钉到 E0→假 Gone） */
            if (XhciBluetoothSeen() && P == XhciBluetoothRootPort()) {
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
                BootLogHexV("Boot: MSC claim force-fail port=", P, 2);
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
                *Sick = 1;
                break;
            }
            if (Arc == 0) {
                continue;
            }

            gTransferSlot = gMscScanSlot;
            if (GetDeviceDesc() < 0) {
                BootLogHex("Boot: MSC claim desc fail port=", P, 2);
                gPortNeedForcePr |= (1u << P);
                DisableSlot(gMscScanSlot);
                gMscScanSlot = 0;
                continue;
            }

            HubRc = TryHubOnRoot(P, Speed, Ok);
            if (HubRc == 1) {
                return 1; /* *Ok 已置；调用方 goto done */
            }
            if (HubRc == 0) {
                if (gXhciCmdSick) {
                    *Sick = 1;
                    break;
                }
                continue;
            }

            if (FinishClaim(P, Speed)) {
                *Ok = 1;
                return 1;
            }
        }
    }
    return 0;
}
