/*
 * ClaimPort.c — 单口 Force / Address / 根口 hub（usb-map / PR-S-xhcimsc-1）
 *
 * ForceUntilPed：freeze 则拒；已 PED+CCS 可软过；否则 Port Reset。
 * Address 失败日志 `addr cc=`（常见 0x04 USBTE / 0x11 Context）→ 规格对表后再改，勿堆重试。
 * 上游 ClaimPorts；全局仍 Xhci.c。
 */
#include "XHCI/Private.h"

/*
 * 口未 PED：Force PR。已 PED+CCS：软过（*Force=0）——冷启动 U 盘常驻 PED，
 * 真机先 Force 易 reset fail。gPortNeedForcePr 仍先 Force。
 */
int ForceUntilPed(UINT32 P, int *Force) {
    UINT32 Ps;
    int Ready = 0;
    UINT32 QuietSave;
    int NeedForce;

    *Force = 0;
    if (gXhciPortEnumFreeze) {
        return 0;
    }
    Ps = ReadMmio32(gOperationalBase + PortReg(P));
    NeedForce = (gPortNeedForcePr & (1u << P)) != 0;
    if (!NeedForce && (Ps & PORTSC_PED) && (Ps & PORTSC_CCS)) {
        BootLogHex("Boot: MSC claim try PED port=", P, 2);
        return 1;
    }

    *Force = 1;
    /* 空口 / USB3 配对口 Force 常 PRC 超时或 not PED；安静探测，只留最终一行 */
    QuietSave = gDiagQuiet;
    gDiagQuiet = 1;
    BootLogHexV("Boot: MSC claim reset force port=", P, 2);
    {
        int Attempt;

        for (Attempt = 0; Attempt < 2 && !Ready; Attempt++) {
            if (Attempt > 0) {
                int W;
                BootLogHexV("Boot: MSC claim Force retry port=", P, 2);
                /* 丢 CCS 后等设备重新出现（Force 过猛常见） */
                for (W = 0; W < 30; W++) {
                    Ps = ReadMmio32(gOperationalBase + PortReg(P));
                    if (Ps & PORTSC_CCS) {
                        break;
                    }
                    StallMs(15);
                }
            }
            if (!ResetPortEx(P, 1)) {
                Ps = ReadMmio32(gOperationalBase + PortReg(P));
                if ((Ps & PORTSC_CCS) && !(Ps & PORTSC_PED)) {
                    int W;
                    /* PRC 已到但 PED 慢：再等一会，勿立刻放弃 */
                    for (W = 0; W < 8; W++) {
                        StallMs(15);
                        Ps = ReadMmio32(gOperationalBase + PortReg(P));
                        if ((Ps & PORTSC_PED) && (Ps & PORTSC_CCS)) {
                            Ready = 1;
                            BootLogHexV("Boot: MSC claim late PED port=", P, 2);
                            break;
                        }
                        if (!(Ps & PORTSC_CCS)) {
                            break;
                        }
                    }
                    /* 口还插着但复位失败：再 Force 只会把超时再等一遍 */
                    if (!Ready) {
                        break;
                    }
                }
                continue;
            }
            Ready = 1;
            if (!HalCpuIsHypervisor()) {
                StallMs(50);
            }
        }
    }
    gDiagQuiet = QuietSave;
    if (!Ready) {
        Ps = ReadMmio32(gOperationalBase + PortReg(P));
        BootLogHex("Boot: XHCI port reset fail=", P, 2);
        BootLogHex("Boot: XHCI reset fail PORTSC=", Ps, 8);
        if (gEnumWhy != 0) {
            BootLog(gEnumWhy);
        }
        return 0;
    }
    Ps = ReadMmio32(gOperationalBase + PortReg(P));
    if (!(Ps & PORTSC_PED) || !(Ps & PORTSC_CCS)) {
        BootLogHex("Boot: XHCI port not PED=", P, 2);
        return 0;
    }
    return 1;
}

/*
 * AddressDeviceOnPort + QEMU/真机 Force 重试。
 * 成功 1；失败续扫 0；cmd sick 中止 -1。写出 *Speed。
 */
int AddressPort(UINT32 P, int Force, UINT8 *Speed) {
    int AddrOk;
    UINT32 QuietSave;
    UINT32 Ps;

    Ps = ReadMmio32(gOperationalBase + PortReg(P));
    *Speed = PortSpeed(Ps);
    QuietSave = gDiagQuiet;
    gDiagQuiet = 1;
    AddrOk = AddressDeviceOnPort(P, *Speed, &gMscScanSlot, gMscScanDevCtx, 0, 0, 0, 0,
                                 0);
    if (!AddrOk && gMscScanSlot != 0) {
        DisableSlot(gMscScanSlot);
        gMscScanSlot = 0;
    }
    /*
     * QEMU：软 Address 失败再 Force。
     * 真机：勿立刻 Force（易 0x6E1）；记 NeedForce，留给后续口 / 热插。
     */
    if (!AddrOk && !Force) {
        if (gXhciCmdSick) {
            gDiagQuiet = QuietSave;
            BootLog("Boot: MSC claim abort (cmd sick after PED Address)\n");
            RecoverCommandRing();
            gXhciCmdSick = 0;
            return -1;
        }
        if (!HalCpuIsHypervisor()) {
            BootLogHex("Boot: MSC claim addr soft fail port=", P, 2);
            gPortNeedForcePr |= (1u << P);
            gDiagQuiet = QuietSave;
            return 0;
        }
        BootLogHex("Boot: MSC claim addr retry Force port=", P, 2);
        if (ResetPortEx(P, 1)) {
            StallMs(50);
            *Speed = PortSpeed(ReadMmio32(gOperationalBase + PortReg(P)));
            AddrOk = AddressDeviceOnPort(P, *Speed, &gMscScanSlot, gMscScanDevCtx,
                                         0, 0, 0, 0, 0);
            if (!AddrOk && gMscScanSlot != 0) {
                DisableSlot(gMscScanSlot);
                gMscScanSlot = 0;
            }
        }
    }
    /* 真机 Address 仍失败（cc=0x04/0x11）：再 Force+Address 一轮 */
    if (!AddrOk && Force && !HalCpuIsHypervisor() &&
        (gCmdCode == 4 || gCmdCode == 0x11) && !gXhciCmdSick) {
        BootLogHex("Boot: MSC claim addr 2nd Force port=", P, 2);
        if (gMscScanSlot != 0) {
            DisableSlot(gMscScanSlot);
            gMscScanSlot = 0;
        }
        StallMs(40);
        if (ResetPortEx(P, 1)) {
            StallMs(100);
            *Speed = PortSpeed(ReadMmio32(gOperationalBase + PortReg(P)));
            AddrOk = AddressDeviceOnPort(P, *Speed, &gMscScanSlot, gMscScanDevCtx,
                                         0, 0, 0, 0, 0);
            if (!AddrOk && gMscScanSlot != 0) {
                DisableSlot(gMscScanSlot);
                gMscScanSlot = 0;
            }
        }
    }
    gDiagQuiet = QuietSave;
    if (!AddrOk) {
        BootLogHex("Boot: MSC claim addr fail port=", P, 2);
        BootLogHex("Boot: MSC claim addr cc=", gCmdCode, 2);
        gPortNeedForcePr |= (1u << P);
        if (gXhciCmdSick) {
            BootLog("Boot: MSC claim abort (cmd sick)\n");
            return -1;
        }
        return 0;
    }
    if (gMscScanSlot <= DCBAA_SLOTS) {
        gSlotEp0UsesKbdRing[gMscScanSlot] = 0;
    }
    gPortNeedForcePr &= ~(1u << P);
    gMscRoute = 0;
    gMscHubSlot = 0;
    gMscTtPort = 0;
    return 1;
}

/*
 * 根口 hub 分支。成功认领 MSC：*Ok=1 且返回 1（goto done）。
 * 非 hub：返回 -1（继续 FinishClaim）。其它：返回 0（continue）。
 */
int TryHubOnRoot(UINT32 P, UINT8 Speed, int *Ok) {
    int Hubish = IsHubDeviceDesc() || (gCtrlBuf[4] == 0x09);

    if (!Hubish && gCtrlBuf[4] == 0) {
        UINT16 Total;

        if (GetDesc(USB_WVALUE_DT_CONFIG, 0, 9, gMscCfgBuf) == 0) {
            Total = (UINT16)(gMscCfgBuf[2] | (gMscCfgBuf[3] << 8));
            if (Total < 9) {
                Total = 9;
            }
            if (Total > sizeof(gMscCfgBuf)) {
                Total = (UINT16)sizeof(gMscCfgBuf);
            }
            if (GetDesc(USB_WVALUE_DT_CONFIG, 0, Total, gMscCfgBuf) == 0 &&
                ConfigHasHubIface(gMscCfgBuf, Total)) {
                Hubish = 1;
                BootLogHex("Boot: MSC claim hub iface root=", P, 2);
            }
        } else {
            RecoverEp0(gMscScanSlot);
            gXferSlot = gMscScanSlot;
        }
    }

    if (!Hubish) {
        return -1;
    }

    {
        UINT32 Was = gMscScanSlot;
        UINT32 HubBefore = gHubSlotId;

        gMscScanSlot = 0;
        BootLogHex("Boot: MSC claim hub on root=", P, 2);
        /*
         * 已有 HID hub 时 ClaimHubOnRootPort 会 DisableSlot(Was)，
         * 正是外接第二 hub（U 盘所在）→ none + 长时间 Stall 像卡死。
         */
        if (HubBefore != 0 && Was != 0 && Was != HubBefore) {
            if (ProbeSecondHub(Was, P, Speed)) {
                *Ok = 1;
                return 1;
            }
            /*
             * SS 侧非 SS 速度的设备：规格走 USB2 hub。ProbeSecondHub
             * 失败后已恢复 gHub*=原 USB2 hub，再扫一轮子口。
             */
            if (gHubSlotId != 0 && gHubSpeed < 4) {
                BootLog("Boot: MSC claim usb2 rescan after ss\n");
                if (EnumerateHubChildren()) {
                    *Ok = 1;
                    return 1;
                }
            }
            return 0;
        }
        if (ClaimHubOnRootPort(P, Speed, Was)) {
            if (!HalCpuIsHypervisor()) {
                StallMs(80); /* PR-BOOT-fast-2：原 150 */
            }
            /* Enumerate 内部已有第二遍。外面再整轮扫会把空口等待再乘一遍。 */
            if (EnumerateHubChildren()) {
                *Ok = 1;
                return 1;
            }
            /*
             * USB2 hub 无 MSC：先保留。SS hub 上非 SS（HubPortSpeed≠4，
             * 即 PORT_SPEED Reserved）的设备在双总线 USB2 侧，须再扫勿 Disable。
             */
            if (HubBefore == 0 && gHubSlotId != 0 && gHubRootPort == P) {
                if (Speed < 4) {
                    BootLog("Boot: MSC claim keep usb2 hub\n");
                } else {
                    DisableSlot(gHubSlotId);
                    gHubSlotId = 0;
                    gHubRootPort = 0;
                    BootLog("Boot: MSC claim hub no msc, release\n");
                }
            }
        } else if (Was != 0) {
            DisableSlot(Was);
        }
        return 0;
    }
}

