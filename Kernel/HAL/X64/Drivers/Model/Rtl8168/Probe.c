/*
 * Probe.c — PCI 查找、BAR、MAC（PR-N-rtl-1）
 */
#include "Rtl8168.h"
#include "Private.h"
#include "PCIe.h"
#include "VirtualMemory.h"
#include "Hal.h"
#include "HalSerial.h"
#include "SerialLog.h"

volatile UINT8 *gRtlBar;
UINT64 gRtlBarPhys;
UINT16 gRtlDid;
UINT8 gRtlMac[6];
int gRtlReady;

static UINT8 gPciBus;
static UINT8 gPciDev;
static UINT8 gPciFn;
static UINT64 gClaimBar;
static UINT16 gClaimDid;
static int gHavePci;

int RtlAccept(UINT8 Bus, UINT8 Dev, UINT8 Fn, UINT64 Bar, UINT16 Did) {
    if (Bar == 0) {
        return 0;
    }
    gPciBus = Bus;
    gPciDev = Dev;
    gPciFn = Fn;
    gClaimBar = Bar;
    gClaimDid = Did;
    gHavePci = 1;
    return 1;
}

static int MacValid(const UINT8 Mac[6]) {
    int i;
    int Zero = 1;
    int Broadcast = 1;

    if (Mac[0] & 0x01u) {
        return 0;
    }
    for (i = 0; i < 6; i++) {
        if (Mac[i] != 0) {
            Zero = 0;
        }
        if (Mac[i] != 0xFFu) {
            Broadcast = 0;
        }
    }
    return !Zero && !Broadcast;
}

int RtlReadMac(UINT8 Mac[6]) {
    int i;

    if (!gRtlBar || !Mac) {
        return 0;
    }
    for (i = 0; i < 6; i++) {
        Mac[i] = RtlMmioR8(RTL_MAC0 + (UINT32)i);
    }
    return MacValid(Mac);
}

static void LogMac(const UINT8 Mac[6]) {
    char Line[64];
    char Hex[12];
    int n = 0;
    const char *P = "Boot: R8169 MAC=";
    int i;

    while (*P && n < 24) {
        Line[n++] = *P++;
    }
    for (i = 0; i < 6; i++) {
        HalSerialFormatHex(Hex, Mac[i], 2);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
        if (i < 5 && n < 62) {
            Line[n++] = ':';
        }
    }
    Line[n++] = '\n';
    Line[n] = 0;
    LogDrv(Line);
}

int RtlSetup(void) {
    UINT64 Bar;
    UINT16 Did;
    UINT32 Cmd;
    UINT8 Mac[6];
    int i;

    if (gRtlReady) {
        return 1;
    }
    if (!VirtualMemoryEnabled() || !gHavePci || gClaimBar == 0) {
        return 0;
    }
    Cmd = PciReadConfig(gPciBus, gPciDev, gPciFn, 0x04);
    PciWriteConfig(gPciBus, gPciDev, gPciFn, 0x04, Cmd | 0x06);
    Bar = gClaimBar;
    Did = gClaimDid;
    if (VirtualMemoryMapRange(Bar, Bar, RTL_BAR_MAP_BYTES,
                              PTE_PRESENT | PTE_WRITABLE) != 0) {
        LogDrv("Boot: R8169 Map BAR Fail\n");
        return 0;
    }
    gRtlBarPhys = Bar;
    gRtlBar = (volatile UINT8 *)(UINTN)Bar;
    gRtlDid = Did;

    if (!RtlReadMac(Mac)) {
        LogDrv("Boot: R8169 MAC Unavailable\n");
        gRtlBar = 0;
        return 0;
    }
    for (i = 0; i < 6; i++) {
        gRtlMac[i] = Mac[i];
    }
    LogMac(Mac);

    if (!RtlBringUp()) {
        LogNet("Boot: R8169 BringUp Fail\n");
        gRtlBar = 0;
        gRtlDid = 0;
        return 0;
    }
    gRtlReady = 1;
    LogNet("Boot: R8169 Ready Poll\n");
    return 1;
}

int RtlReady(void) {
    return gRtlReady;
}

void RtlGetMac(UINT8 Mac[6]) {
    int i;
    if (!Mac) {
        return;
    }
    for (i = 0; i < 6; i++) {
        Mac[i] = gRtlReady ? gRtlMac[i] : 0;
    }
}

UINT16 RtlPciDid(void) {
    return gRtlDid;
}
