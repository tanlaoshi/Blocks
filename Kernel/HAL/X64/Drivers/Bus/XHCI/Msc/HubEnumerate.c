/*
 * HubEnumMsc.c — PR-S-xhcihub-1：hub 子口 MSC 枚举与第二 hub
 *
 * 从 Hub.c 原样搬家；不改语义。Hub 口帮手去掉 static，声明在 Internal.h。
 */
#include "XHCI/Private.h"

/*
 * PR-H-msc-4：hub 子口找 MSC（Bulk）；跳过已占用键/鼠子口。
 * Address → FinishClaim（SetConfig+Bulk，无 SCSI）。
 * 真机：先统一上电再扫，空口略等（U 盘上电慢 → 误判 hub empty）。
 * PR-BOOT-fast-2：Pass/空口等待收紧；仍保留一轮 rescan + 软退。
 */
/* 扫盘复位了鼠标口但槽号还在时，清掉槽，桌面初始化才会补绑。 */
static void NoteMouseHubDropped(void) {
    UINT32 St = 0;

    if (gMouseSlotId == 0 || gMouseTtPort == 0) {
        return;
    }
    if (gMouseHubSlot != 0 && gMouseHubSlot != (UINT8)gHubSlotId) {
        return;
    }
    if (HubGetPortStatus(gMouseTtPort, &St) == 0 &&
        (St & HUB_PORT_CONNECTION) && (St & HUB_PORT_ENABLE)) {
        return;
    }
    BootLog("Boot: MSC hub dropped mouse\n");
    DisableSlot(gMouseSlotId);
}

int EnumerateHubChildren(void) {
    UINT8 Port;
    UINT8 MaxP = gHubNumPorts;
    int Pass;

    if (gHubSlotId == 0 || gMscClaimed) {
        return 0;
    }
    if (MaxP == 0 || MaxP > 15) {
        MaxP = 8;
    }
    BootLog("Boot: MSC claim hub children\n");
    BootLogHex("Boot: MSC claim hub slot=", gHubSlotId, 2);
    BootLogHex("Boot: MSC claim hub nports=", MaxP, 2);
    HalBootLogoBump(82);

    /* Pass0：全部上电；Pass1：读状态并认领（再扫一轮） */
    for (Port = 1; Port <= MaxP; Port++) {
        (void)HubSetPortFeat(Port, HUB_FEAT_PORT_POWER);
    }
    HalBootLogoBump(83);
    if (!HalCpuIsHypervisor()) {
        StallMs(150); /* 原 250 */
    } else {
        volatile int D;
        for (D = 0; D < 80000; D++) {
        }
    }
    HalBootLogoBump(84);

    for (Pass = 0; Pass < 2; Pass++) {
        if (Pass > 0 && !HalCpuIsHypervisor()) {
            BootLogHex("Boot: MSC claim hub rescan pass=", (UINT32)Pass, 1);
            HalBootLogoBump(91);
            StallMs(100); /* 原 200 */
        }
        for (Port = 1; Port <= MaxP; Port++) {
            UINT32 St = 0;
            UINT8 Speed;
            int t;
            int Wait;

            if (HubGetPortStatus(Port, &St) < 0) {
                BootLogHex("Boot: MSC claim hub status fail port=", Port, 2);
                continue;
            }
            if (!(St & HUB_PORT_CONNECTION)) {
                /* 只在第一遍等晚到的设备。第二遍空口不再空等。 */
                if (Pass == 0) {
                    for (Wait = 0; Wait < 2 && !(St & HUB_PORT_CONNECTION); Wait++) {
                        if (!HalCpuIsHypervisor()) {
                            StallMs(10);
                        }
                        if (HubGetPortStatus(Port, &St) < 0) {
                            break;
                        }
                    }
                }
                if (!(St & HUB_PORT_CONNECTION)) {
                    if (Pass == 1) {
                        BootLogHex("Boot: MSC claim hub empty port=", Port, 2);
                        HalBootLogoBump(91);
                    }
                    continue;
                }
            }
            if ((gKbdRoute & 0xF) == (UINT32)Port && gSlotId != 0) {
                BootLogHex("Boot: MSC claim hub skip kbd port=", Port, 2);
                continue;
            }
            if ((gMouseRoute & 0xF) == (UINT32)Port && gMouseSlotId != 0) {
                BootLogHex("Boot: MSC claim hub skip mouse port=", Port, 2);
                continue;
            }
            if (gMouseSlotId != 0 && gMouseTtPort == Port &&
                (gMouseHubSlot == 0 || gMouseHubSlot == (UINT8)gHubSlotId)) {
                BootLogHex("Boot: MSC claim hub skip mouse tt=", Port, 2);
                continue;
            }
            if (gFtdiClaimed && gFtdiHubSlot != 0 && gFtdiTtPort == Port) {
                continue;
            }
            BootLogHex("Boot: MSC claim hub port=", Port, 2);
            BootLogHex("Boot: MSC claim hub st=", St, 4);
            HalBootLogoBump(91);
            /*
             * PORT_SPEED（USB3 Table 10-10）：字段 0=5Gbps，不是「未协商」。
             * Address 前必须复位到 Default：已 Enable 的 SS 口若跳过复位，
             * 设备可能已在 Addressed，SET_ADDRESS→无应答→命令环超时（本机
             * st=0x0203 / speed=4 后 AddressDev 超时→cmd sick→鼠 PPT）。
             * SS：Warm Reset（BH_PORT_RESET §10.14.2.4）；USB2：Hot Reset。
             */
            Speed = HubPortSpeed(St);
            if (gHubSpeed >= 4) {
                if (HubSetPortFeat(Port, HUB_FEAT_BH_PORT_RESET) < 0) {
                    BootLogHex("Boot: MSC claim hub BH reset fail port=", Port, 2);
                    continue;
                }
                for (t = 0; t < (HalCpuIsHypervisor() ? 50000 : 100); t++) {
                    if (HubGetPortStatus(Port, &St) < 0) {
                        break;
                    }
                    if (St & HUB_C_BH_PORT_RESET) {
                        (void)HubClearPortFeat(Port, HUB_FEAT_C_BH_PORT_RESET);
                        break;
                    }
                    if (St & HUB_C_PORT_RESET) {
                        (void)HubClearPortFeat(Port, HUB_FEAT_C_PORT_RESET);
                    }
                    if (!HalCpuIsHypervisor()) {
                        StallMs(5);
                    }
                }
                for (t = 0; t < (HalCpuIsHypervisor() ? 20000 : 100); t++) {
                    if (HubGetPortStatus(Port, &St) < 0) {
                        break;
                    }
                    if (St & HUB_C_PORT_CONNECTION) {
                        (void)HubClearPortFeat(Port, HUB_FEAT_C_PORT_CONNECTION);
                    }
                    if (St & HUB_C_BH_PORT_RESET) {
                        (void)HubClearPortFeat(Port, HUB_FEAT_C_BH_PORT_RESET);
                    }
                    if ((St & HUB_PORT_ENABLE) && HubPortSpeed(St) == 4) {
                        break;
                    }
                    if (!HalCpuIsHypervisor()) {
                        StallMs(5);
                    }
                }
                Speed = HubPortSpeed(St);
                if (!(St & HUB_PORT_ENABLE) || Speed != 4) {
                    BootLogHex("Boot: MSC claim hub warm not SS port=", Port, 2);
                    BootLogHex("Boot: MSC claim hub st=", St, 4);
                    BootLogHex("Boot: MSC claim hub speed=", Speed, 1);
                    continue;
                }
                BootLogHex("Boot: MSC claim hub warm ok st=", St, 4);
            } else {
                if (HubSetPortFeat(Port, HUB_FEAT_PORT_RESET) < 0) {
                    continue;
                }
                for (t = 0; t < (HalCpuIsHypervisor() ? 50000 : 80); t++) {
                    if (HubGetPortStatus(Port, &St) < 0) {
                        break;
                    }
                    if (St & HUB_C_PORT_RESET) {
                        (void)HubClearPortFeat(Port, HUB_FEAT_C_PORT_RESET);
                        break;
                    }
                    if (!HalCpuIsHypervisor()) {
                        StallMs(5);
                    }
                }
                for (t = 0; t < (HalCpuIsHypervisor() ? 20000 : 80); t++) {
                    if (HubGetPortStatus(Port, &St) < 0) {
                        break;
                    }
                    if (St & HUB_C_PORT_CONNECTION) {
                        (void)HubClearPortFeat(Port, HUB_FEAT_C_PORT_CONNECTION);
                    }
                    if (St & HUB_PORT_ENABLE) {
                        break;
                    }
                    if (!HalCpuIsHypervisor()) {
                        StallMs(5);
                    }
                }
                if (!(St & HUB_PORT_ENABLE)) {
                    BootLogHex("Boot: MSC claim hub not en port=", Port, 2);
                    continue;
                }
                Speed = HubPortSpeed(St);
            }
            if (gMscScanSlot != 0) {
                DisableSlot(gMscScanSlot);
                gMscScanSlot = 0;
            }
            gMscRoute = (UINT32)Port;
            gMscHubSlot = (UINT8)gHubSlotId;
            gMscTtPort = Port;
            BootLogHex("Boot: MSC claim hub speed=", Speed, 1);
            if (!AddressDeviceOnPort(gHubRootPort, Speed, &gMscScanSlot, gMscScanDevCtx,
                                     (UINT32)Port, (UINT8)gHubSlotId, Port, 0, 0)) {
                if (gMscScanSlot != 0) {
                    DisableSlot(gMscScanSlot);
                    gMscScanSlot = 0;
                }
                BootLogHex("Boot: MSC claim hub addr fail port=", Port, 2);
                if (gXhciCmdSick) {
                    BootLog("Boot: MSC claim hub abort (cmd sick)\n");
                    NoteMouseHubDropped();
                    return 0;
                }
                (void)HubSetPortFeat(Port, gHubSpeed >= 4 ? HUB_FEAT_BH_PORT_RESET
                                                          : HUB_FEAT_PORT_RESET);
                continue;
            }
            if (gMscScanSlot <= DCBAA_SLOTS) {
                gSlotEp0UsesKbdRing[gMscScanSlot] = 0;
            }
            if (FinishClaim(gHubRootPort, Speed)) {
                BootLog("Boot: MSC claim via hub\n");
                NoteMouseHubDropped();
                return 1;
            }
            if (gMscScanSlot != 0) {
                DisableSlot(gMscScanSlot);
                gMscScanSlot = 0;
            }
        }
    }
    NoteMouseHubDropped();
    return 0;
}

/*
 * HID 已占用 gHubSlotId 时，外接第二 hub（ExistingSlot）带 U 盘：
 * 勿走 ClaimHubOnRootPort（会 DisableSlot 第二 hub）。临时切 gHub* 扫 MSC。
 */
int ProbeSecondHub(UINT32 HubSlot, UINT32 RootPort, UINT8 Speed) {
    UINT32 SavedSlot = gHubSlotId;
    UINT32 SavedRoot = gHubRootPort;
    UINT8 SavedPorts = gHubNumPorts;
    UINT8 SavedSpeed = gHubSpeed;
    UINT8 SavedMtt = gHubMtt;
    UINT8 SavedTtt = gHubTtt;
    UINT8 Nports = 4;
    int Usb2Hub = (Speed < 4);
    int Ok;

    if (HubSlot == 0 || HubSlot == gHubSlotId) {
        return EnumerateHubChildren();
    }

    BootLogHex("Boot: MSC claim 2nd hub slot=", HubSlot, 2);
    BootLogHex("Boot: MSC claim 2nd hub root=", RootPort, 2);

    /* Ep0 走 msc 环，勿 InitRing(gHubEp0) 毁掉 HID hub dequeue */
    gMscProbeHubSlot = HubSlot;
    gHubSlotId = HubSlot;
    gHubRootPort = RootPort;
    gHubSpeed = Speed;
    gXferSlot = HubSlot;
    gEp0Mps = SpeedMps(Speed);
    RecoverEp0(HubSlot);
    HubNoteMttFromDevDesc(Speed);

    if (!FinishHubSetup(&Nports)) {
        BootLog("Boot: MSC claim 2nd hub cfg fail\n");
        DisableSlot(HubSlot);
        gMscProbeHubSlot = 0;
        gHubSlotId = SavedSlot;
        gHubRootPort = SavedRoot;
        gHubNumPorts = SavedPorts;
        gHubSpeed = SavedSpeed;
        gHubMtt = SavedMtt;
        gHubTtt = SavedTtt;
        return 0;
    }
    if (!EvaluateHubSlot(HubSlot, RootPort, Speed, Nports)) {
        BootLog("Boot: MSC claim 2nd hub eval skip\n");
    }
    (void)Usb2Hub;

    Ok = EnumerateHubChildren();
    gMscProbeHubSlot = 0;

    if (Ok) {
        BootLog("Boot: MSC claim 2nd hub keep parent\n");
        if (SavedSlot != 0) {
            gHubSlotId = SavedSlot;
            gHubRootPort = SavedRoot;
            gHubNumPorts = SavedPorts;
            gHubSpeed = SavedSpeed;
            gHubMtt = SavedMtt;
            gHubTtt = SavedTtt;
        }
        return 1;
    }

    BootLog("Boot: MSC claim 2nd hub no msc\n");
    DisableSlot(HubSlot);
    /*
     * DisableSlot 后设备仍 PED/Addressed；不 Force PR/WPR 再 Address → cc=0x04
     *（Port.c：二次枚举须 PR）。随后 UART/Wifi 扫到此根口会抢命令环、鼠假死。
     */
    (void)ResetPortEx(RootPort, 1);
    gHubSlotId = SavedSlot;
    gHubRootPort = SavedRoot;
    gHubNumPorts = SavedPorts;
    gHubSpeed = SavedSpeed;
    gHubMtt = SavedMtt;
    gHubTtt = SavedTtt;
    gMscRoute = 0;
    gMscHubSlot = 0;
    gMscTtPort = 0;
    return 0;
}

