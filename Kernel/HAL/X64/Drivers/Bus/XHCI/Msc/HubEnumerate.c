/*
 * HubEnumerate.c — hub 子口 MSC 枚举（usb-map / usb-link-cc）
 *
 * GetPortStatus → Hot/Warm Reset（见 C_*）→ Address（SS：BSR1+BSR0）→ FinishClaim。
 * 父 hub 须已 SetHubDepth。同一子口 Address USBTE 后：一次掉电重上电再试，禁止
 * Warm/rescan 同招连撞（标准优先：换方法，勿重试风暴）。
 *
 * 子口复位 / 掉电 / 鼠掉线 → HubEnumerateReset.c（PR-NORM-11）。
 */
#include "XHCI/Private.h"

/*
 * PR-H-msc-4：hub 子口找 MSC（Bulk）；跳过已占用键/鼠子口。
 * Address → FinishClaim（SetConfig+Bulk，无 SCSI）。
 * 真机：先统一上电再扫，空口略等（U 盘上电慢 → 误判 hub empty）。
 * PR-BOOT-fast-2：Pass/空口等待收紧；仍保留一轮 rescan + 软退。
 */
int EnumerateHubChildren(void) {
    UINT8 Port;
    UINT8 MaxP = gHubNumPorts;
    int Pass;
    UINT16 AddrFailed = 0; /* bit N：子口 N 已 Address 失败，Pass1 勿同招再撞 */

    if (gHubSlotId == 0 || gMscClaimed) {
        return 0;
    }
    if (MaxP == 0 || MaxP > 15) {
        MaxP = 8;
    }
    BootLogV("Boot: MSC claim hub children\n");
    BootLogHexV("Boot: MSC claim hub slot=", gHubSlotId, 2);
    BootLogHexV("Boot: MSC claim hub nports=", MaxP, 2);
    HalBootLogoBump(82);

    /* Pass0：全部上电；Pass1：读状态并认领（再扫一轮） */
    for (Port = 1; Port <= MaxP; Port++) {
        (void)HubSetPortFeature(Port, HUB_FEAT_PORT_POWER);
    }
    HalBootLogoBump(83);
    if (!HalCpuIsHypervisor()) {
        StallMilliseconds(150); /* 原 250 */
    } else {
        volatile int D;
        for (D = 0; D < 80000; D++) {
        }
    }
    HalBootLogoBump(84);

    for (Pass = 0; Pass < 2; Pass++) {
        if (Pass > 0 && !HalCpuIsHypervisor()) {
            BootLogHexV("Boot: MSC claim hub rescan pass=", (UINT32)Pass, 1);
            HalBootLogoBump(91);
            StallMilliseconds(100); /* 原 200 */
        }
        for (Port = 1; Port <= MaxP; Port++) {
            UINT32 St = 0;
            UINT8 Speed;
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
                            StallMilliseconds(10);
                        }
                        if (HubGetPortStatus(Port, &St) < 0) {
                            break;
                        }
                    }
                }
                if (!(St & HUB_PORT_CONNECTION)) {
                    if (Pass == 1) {
                        BootLogHexV("Boot: MSC claim hub empty port=", Port, 2);
                        BootLogHexV("Boot: MSC claim hub empty st=", St, 4);
                        HalBootLogoBump(91);
                    }
                    continue;
                }
            }
            if ((gKeyboardRoute & 0xF) == (UINT32)Port && gSlotId != 0) {
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
            /* Pass1 只收晚到连接；Address 已失败的口禁止同招再撞 */
            if (Pass > 0 && (AddrFailed & (1u << Port)) != 0) {
                continue;
            }
            BootLogHexV("Boot: MSC claim hub port=", Port, 2);
            BootLogHexV("Boot: MSC claim hub st=", St, 4);
            HalBootLogoBump(91);
            if (!HubResetChildForAddress(Port, &St, &Speed, 0)) {
                continue;
            }
            if (gMscScanSlot != 0) {
                DisableSlot(gMscScanSlot);
                gMscScanSlot = 0;
            }
            gMscRoute = (UINT32)Port;
            gMscHubSlot = (UINT8)gHubSlotId;
            gMscTtPort = Port;
            BootLogHexV("Boot: MSC claim hub speed=", Speed, 1);
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
                /*
                 * 换方法：端口掉电重上电（非再 Warm/再 Address）。
                 * 仍失败则记 AddrFailed，Pass1 跳过该口。
                 */
                if (HubPowerCyclePort(Port) &&
                    HubResetChildForAddress(Port, &St, &Speed, 0) &&
                    AddressDeviceOnPort(gHubRootPort, Speed, &gMscScanSlot,
                                        gMscScanDevCtx, (UINT32)Port,
                                        (UINT8)gHubSlotId, Port, 0, 0)) {
                    /* fall through FinishClaim */
                } else {
                    if (gMscScanSlot != 0) {
                        DisableSlot(gMscScanSlot);
                        gMscScanSlot = 0;
                    }
                    if (Port < 16) {
                        AddrFailed |= (UINT16)(1u << Port);
                    }
                    BootLogHex("Boot: MSC claim hub give up port=", Port, 2);
                    continue;
                }
            }
            if (gMscScanSlot <= DCBAA_SLOTS) {
                gSlotEndpoint0UsesKeyboardRing[gMscScanSlot] = 0;
            }
            if (FinishClaim(gHubRootPort, Speed)) {
                BootLogV("Boot: MSC claim via hub\n");
                NoteMouseHubDropped();
                return 1;
            }
            if (gMscScanSlot != 0) {
                DisableSlot(gMscScanSlot);
                gMscScanSlot = 0;
            }
            if (Port < 16) {
                AddrFailed |= (UINT16)(1u << Port);
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

    BootLogHexV("Boot: MSC claim 2nd hub slot=", HubSlot, 2);
    BootLogHexV("Boot: MSC claim 2nd hub root=", RootPort, 2);

    /* Endpoint0 走 msc 环，勿 RingInitialize(gHubEndpoint0) 毁掉 HID hub dequeue */
    gMscProbeHubSlot = HubSlot;
    gHubSlotId = HubSlot;
    gHubRootPort = RootPort;
    gHubSpeed = Speed;
    gTransferSlot = HubSlot;
    gEndpoint0MaxPacketSize = SpeedMaxPacketSize(Speed);
    RecoverEndpoint0(HubSlot);
    HubNoteMttFromDeviceDescriptor(Speed);

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
    if (!Usb2Hub && !HubSetDepth(0)) {
        BootLog("Boot: MSC claim 2nd hub depth fail\n");
    }

    Ok = EnumerateHubChildren();
    gMscProbeHubSlot = 0;

    if (Ok) {
        BootLogV("Boot: MSC claim 2nd hub keep parent\n");
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

