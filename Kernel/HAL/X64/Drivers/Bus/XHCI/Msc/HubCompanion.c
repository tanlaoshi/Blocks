/*
 * HubCompanion.c — SS hub 无 MSC 后：伴生 HS hub + 其它根口直挂 MSC
 *
 * USB3 hub = SS + HS 两套；USB2 棒只出现在伴生 HS（另一 xHCI 根口）。
 * 证据：本机 SS@0x10 四子口 CONNECTION=0 → companion none（无 try）=
 * 伴生口当时无 CCS。须 keep SS、等伴生 CCS 出现再 Address（规格双总线）。
 * 禁对键鼠/BT 口 Force。
 */
#include "XHCI/Private.h"

static int CompanionPortOwned(UINT32 P) {
    if (P == gHubRootPort) {
        return 1;
    }
    if (gSlotId != 0 && P == gPort1) {
        return 1;
    }
    /* 仅根口鼠：hub 后鼠 gMousePort==hub 根，不能当「整根伴生禁区」 */
    if (gMouseSlotId != 0 && gMouseHubSlot == 0 && P == gMousePort) {
        return 1;
    }
    if (XhciBluetoothSeen() && P == XhciBluetoothRootPort()) {
        return 1;
    }
    if (gFtdiClaimed && P == gFtdiPort) {
        return 1;
    }
    if (gCdcClaimed && P == gCdcPort) {
        return 1;
    }
    return 0;
}

/* 仅 VERBOSE；默认可读路径不刷 survey（曾拆成多行+空行） */
static void SurveyRootCcs(const char *Tag) {
    UINT32 P;
    UINT32 N = 0;

    if (!DiagVerbose()) {
        (void)Tag;
        return;
    }
    for (P = 1; P <= gMaxPorts && P <= 32u; P++) {
        UINT32 Ps = ReadMmio32(gOperationalBase + PortReg(P));

        if (!(Ps & PORTSC_CCS)) {
            continue;
        }
        N++;
        BootLog(Tag);
        BootLogHex(" ccs port=", P, 2);
        BootLogHex(" portsc=", Ps, 8);
        BootLogHex(" spd=", PortSpeed(Ps), 1);
        BootLog(CompanionPortOwned(P) ? " owned\n" : " free\n");
    }
    BootLogHex("Boot: MSC claim companion ccs n=", N, 2);
}

static int CompanionAddressRoot(UINT32 P, UINT8 *SpeedOut) {
    int Force = 0;

    if (!ForceUntilPed(P, &Force)) {
        BootLogHex("Boot: MSC claim companion force-fail port=", P, 2);
        return 0;
    }
    if (gMscScanSlot != 0) {
        DisableSlot(gMscScanSlot);
        gMscScanSlot = 0;
    }
    if (AddressPort(P, Force, SpeedOut) <= 0) {
        BootLogHex("Boot: MSC claim companion addr-fail port=", P, 2);
        return 0;
    }
    return 1;
}

/* 扫一轮当前 free+CCS 根口；1=已认 MSC */
static int CompanionScanOnce(UINT32 SavedHub) {
    UINT32 P;

    for (P = 1; P <= gMaxPorts && P <= 32u; P++) {
        UINT32 Ps;
        UINT8 Speed;
        UINT32 CompSlot;

        if (CompanionPortOwned(P)) {
            continue;
        }
        Ps = ReadMmio32(gOperationalBase + PortReg(P));
        if (!(Ps & PORTSC_CCS)) {
            continue;
        }
        Speed = PortSpeed(Ps);
        /*
         * 未 PED 时常 spd=0；勿当 LS/FS 跳过——伴生 hub 上电前无速度字段。
         * 真 LS/FS 且已 PED：跳过（几乎不是 MSC hub）。
         */
        if (!HalCpuIsHypervisor() && (Ps & PORTSC_PED) && Speed > 0 &&
            Speed < 3) {
            BootLogHexV("Boot: MSC claim companion skip ls/fs port=", P, 2);
            continue;
        }
        BootLogHexV("Boot: MSC claim companion try port=", P, 2);
        BootLogHexV("Boot: MSC claim companion portsc=", Ps, 8);
        if (!CompanionAddressRoot(P, &Speed)) {
            if (gXhciCmdSick) {
                return 0;
            }
            continue;
        }
        gXferSlot = gMscScanSlot;
        if (GetDeviceDesc() < 0) {
            BootLogHex("Boot: MSC claim companion desc-fail port=", P, 2);
            DisableSlot(gMscScanSlot);
            gMscScanSlot = 0;
            continue;
        }
        if (!IsHubDeviceDesc() && gCtrlBuf[4] != 0x09) {
            BootLogHex("Boot: MSC claim companion not-hub class=", gCtrlBuf[4],
                       2);
            DisableSlot(gMscScanSlot);
            gMscScanSlot = 0;
            continue;
        }
        CompSlot = gMscScanSlot;
        gMscScanSlot = 0;
        if (ProbeSecondHub(CompSlot, P, Speed)) {
            BootLogHex("Boot: MSC claim companion ok root=", P, 2);
            return 1;
        }
        BootLogHex("Boot: MSC claim companion 2nd-hub miss root=", P, 2);
        if (gHubSlotId != SavedHub) {
            gHubSlotId = SavedHub;
        }
    }
    return 0;
}

int ProbeCompanionRootHubs(void) {
    UINT32 SavedHub = gHubSlotId;
    int Round;

    if (SavedHub == 0 || gHubSpeed < 4 || gMscClaimed || gOperationalBase == 0) {
        return 0;
    }
    BootLog("Boot: MSC claim companion roots begin\n");
    SurveyRootCcs("Boot: MSC claim companion");
    /*
     * SS hub 配完后伴生 HS 根口 CCS 常晚到（本机：begin 后立刻 none、无 try）。
     * 规格双总线；等 CCS 出现再 Address，勿盲 Force 键鼠口。
     */
    for (Round = 0; Round < 10; Round++) {
        if (Round > 0) {
            if (!HalCpuIsHypervisor()) {
                StallMs(50);
            }
            SurveyRootCcs("Boot: MSC claim companion wait");
        } else if (!HalCpuIsHypervisor()) {
            StallMs(50);
        }
        if (CompanionScanOnce(SavedHub)) {
            return 1;
        }
        if (gXhciCmdSick) {
            return 0;
        }
    }
    BootLog("Boot: MSC claim companion roots none\n");
    return 0;
}

int ProbeRootStorageAfterHub(void) {
    UINT32 P;
    UINT32 SavedHub = gHubSlotId;
    int Round;

    if (SavedHub == 0 || gMscClaimed || gOperationalBase == 0) {
        return 0;
    }
    BootLog("Boot: MSC claim root storage after hub begin\n");
    for (Round = 0; Round < 4; Round++) {
        if (Round > 0 && !HalCpuIsHypervisor()) {
            StallMs(50);
        }
        SurveyRootCcs("Boot: MSC claim rootstg");
        for (P = 1; P <= gMaxPorts && P <= 32u; P++) {
            UINT32 Ps;
            UINT8 Speed;

            if (CompanionPortOwned(P)) {
                continue;
            }
            Ps = ReadMmio32(gOperationalBase + PortReg(P));
            if (!(Ps & PORTSC_CCS)) {
                continue;
            }
            Speed = PortSpeed(Ps);
            if (!HalCpuIsHypervisor() && (Ps & PORTSC_PED) && Speed > 0 &&
                Speed < 3) {
                continue;
            }
            BootLogHex("Boot: MSC claim root storage try port=", P, 2);
            BootLogHex("Boot: MSC claim root storage portsc=", Ps, 8);
            if (!CompanionAddressRoot(P, &Speed)) {
                if (gXhciCmdSick) {
                    return 0;
                }
                continue;
            }
            gXferSlot = gMscScanSlot;
            if (GetDeviceDesc() < 0) {
                DisableSlot(gMscScanSlot);
                gMscScanSlot = 0;
                continue;
            }
            if (IsHubDeviceDesc() || gCtrlBuf[4] == 0x09) {
                DisableSlot(gMscScanSlot);
                gMscScanSlot = 0;
                continue;
            }
            if (FinishClaim(P, Speed)) {
                BootLogHex("Boot: MSC claim root storage ok port=", P, 2);
                return 1;
            }
            if (gMscScanSlot != 0) {
                DisableSlot(gMscScanSlot);
                gMscScanSlot = 0;
            }
        }
    }
    BootLog("Boot: MSC claim root storage none\n");
    return 0;
}
