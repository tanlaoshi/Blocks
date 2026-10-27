/*
 * BootWatch.c — U 盘起 boot-watch（PORTSC.CCS → Gone 大窗）
 *
 * Arm 在 FS claim 前；ClaimPorts 收口 AfterClaim 钉/清；
 * PollUnplug 调 CcsLost（须曾见 CCS=1，连续 2 拍无连接才认拔）。
 */
#include "XHCI/Private.h"

void XhciMscArmBootWatch(int On) {
    gMscBootWatchArm = On ? 1 : 0;
    if (!On) {
        gMscBootWatchPort = 0;
    }
}

int XhciMscBootWatchCcsLost(UINT32 *OutPort) {
    UINT32 Ps;
    static UINT32 sLostStreak;
    static int sSawConnected;

    if (gMscBootWatchPort == 0 || gOperationalBase == 0) {
        sLostStreak = 0;
        sSawConnected = 0;
        return 0;
    }
    if (gMscBootWatchPort > gMaxPorts) {
        gMscBootWatchPort = 0;
        gMscBootWatchArm = 0;
        sLostStreak = 0;
        sSawConnected = 0;
        return 0;
    }
    Ps = ReadMmio32(gOperationalBase + PortReg(gMscBootWatchPort));
    if (Ps & PORTSC_CCS) {
        sLostStreak = 0;
        sSawConnected = 1;
        return 0;
    }
    if (!sSawConnected) {
        gMscBootWatchPort = 0;
        gMscBootWatchArm = 0;
        sLostStreak = 0;
        return 0;
    }
    sLostStreak++;
    if (sLostStreak < 2u) {
        return 0;
    }
    sLostStreak = 0;
    sSawConnected = 0;
    if (OutPort) {
        *OutPort = gMscBootWatchPort;
    }
    return 1;
}

void XhciMscBootWatchClear(void) {
    gMscBootWatchPort = 0;
    gMscBootWatchArm = 0;
}

void XhciMscBootWatchAfterClaim(int Ok) {
    UINT32 WatchPs;
    UINT32 WatchPort;

    if (Ok && gMscBootWatchArm && gMscPort != 0) {
        gMscBootWatchPort = gMscPort;
        BootLogHex("Boot: MSC boot-watch keep port=", gMscBootWatchPort, 2);
        return;
    }
    if (Ok) {
        gMscBootWatchPort = 0;
        gMscBootWatchArm = 0;
        return;
    }
    if (!gMscBootWatchArm) {
        return;
    }
    WatchPort = 0;
    if (gHubSlotId != 0 && gHubRootPort != 0) {
        WatchPort = gHubRootPort;
    } else if (gMscBootWatchPort != 0) {
        WatchPort = gMscBootWatchPort;
    }
    if (WatchPort == 0) {
        gMscBootWatchArm = 0;
        return;
    }
    WatchPs = ReadMmio32(gOperationalBase + PortReg(WatchPort));
    if (WatchPs & PORTSC_CCS) {
        gMscBootWatchPort = WatchPort;
        BootLogHex("Boot: MSC boot-watch port=", gMscBootWatchPort, 2);
    } else {
        BootLogHex("Boot: MSC boot-watch clear (no CCS) port=", WatchPort, 2);
        gMscBootWatchPort = 0;
        gMscBootWatchArm = 0;
    }
}
