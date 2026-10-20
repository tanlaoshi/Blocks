/*
 * AlxProbe.c — PCI 查找、BAR、永久 MAC（PR-N-alx-1）；Setup 末尾 BringUp（alx-2）
 */
#include "Alx.h"
#include "AlxPrivate.h"
#include "PCIe.h"
#include "VirtualMemory.h"
#include "Hal.h"
#include "HalSerial.h"
#include "SerialLog.h"

volatile UINT8 *gAlxBar;
UINT64 gAlxBarPhys;
UINT16 gAlxDid;
UINT8 gAlxRev;
UINT8 gAlxMac[6];
int gAlxReady;
UINT32 gAlxRxCtrl;
UINT32 gAlxLinkMbps;
int gAlxLinkFull;

static UINT8 gPciBus;
static UINT8 gPciDev;
static UINT8 gPciFn;
static UINT64 gClaimBar;
static UINT16 gClaimDid;
static int gHavePci;

int AlxAccept(UINT8 Bus, UINT8 Dev, UINT8 Fn, UINT64 Bar, UINT16 Did) {
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

int AlxReadMac(UINT8 Mac[6]) {
    UINT32 Mac0;
    UINT32 Mac1;

    if (!gAlxBar || !Mac) {
        return 0;
    }
    Mac0 = AlxMmioR32(ALX_STAD0);
    Mac1 = AlxMmioR32(ALX_STAD1);
    Mac[0] = (UINT8)((Mac1 >> 8) & 0xFFu);
    Mac[1] = (UINT8)(Mac1 & 0xFFu);
    Mac[2] = (UINT8)((Mac0 >> 24) & 0xFFu);
    Mac[3] = (UINT8)((Mac0 >> 16) & 0xFFu);
    Mac[4] = (UINT8)((Mac0 >> 8) & 0xFFu);
    Mac[5] = (UINT8)(Mac0 & 0xFFu);
    return MacValid(Mac);
}

static int WaitClear(UINT32 Reg, UINT32 Bit, int Spins) {
    while (Spins-- > 0) {
        if ((AlxMmioR32(Reg) & Bit) == 0) {
            return 1;
        }
        HalCpuRelax();
    }
    return 0;
}

static int WaitReady(UINT32 Reg, UINT32 BusyMask, int Spins) {
    while (Spins-- > 0) {
        UINT32 V = AlxMmioR32(Reg);
        if ((V & BusyMask) == 0) {
            return 1;
        }
        HalCpuRelax();
    }
    return 0;
}

int AlxLoadPermMac(UINT8 Mac[6]) {
    UINT32 Val;

    if (AlxReadMac(Mac)) {
        return 1;
    }
    if (WaitReady(ALX_SLD, ALX_SLD_STAT | ALX_SLD_START, 100000)) {
        Val = AlxMmioR32(ALX_SLD);
        AlxMmioW32(ALX_SLD, Val | ALX_SLD_START);
        if (WaitClear(ALX_SLD, ALX_SLD_START, 100000) && AlxReadMac(Mac)) {
            return 1;
        }
    }
    Val = AlxMmioR32(ALX_EFLD);
    if (Val & (ALX_EFLD_F_EXIST | ALX_EFLD_E_EXIST)) {
        if (!WaitReady(ALX_EFLD, ALX_EFLD_STAT | ALX_EFLD_START, 100000)) {
            return 0;
        }
        AlxMmioW32(ALX_EFLD, Val | ALX_EFLD_START);
        if (WaitClear(ALX_EFLD, ALX_EFLD_START, 100000) && AlxReadMac(Mac)) {
            return 1;
        }
    }
    return 0;
}

static void LogMac(const UINT8 Mac[6]) {
    char Line[64];
    char Hex[12];
    int n = 0;
    const char *P = "Boot: Alx MAC=";
    int i;

    while (*P && n < 20) {
        Line[n++] = *P++;
    }
    for (i = 0; i < 6; i++) {
        /* FormatHex → "0x.."；数字从 [2] 起（同 ehci-1c） */
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

int AlxSetup(void) {
    UINT64 Bar;
    UINT16 Did;
    UINT8 Rev;
    UINT32 Cmd;
    UINT8 Mac[6];
    int i;

    if (gAlxReady) {
        return 1;
    }
    if (!VirtualMemoryEnabled() || !gHavePci || gClaimBar == 0) {
        return 0;
    }
    Cmd = PciReadConfig(gPciBus, gPciDev, gPciFn, 0x04);
    PciWriteConfig(gPciBus, gPciDev, gPciFn, 0x04, Cmd | 0x06);
    Bar = gClaimBar;
    Did = gClaimDid;
    Rev = (UINT8)(PciReadConfig(gPciBus, gPciDev, gPciFn, 0x08) & 0xFFu);
    if (VirtualMemoryMapRange(Bar, Bar, ALX_BAR_MAP_BYTES,
                              PTE_PRESENT | PTE_WRITABLE) != 0) {
        LogDrv("Boot: Alx Map BAR Fail\n");
        return 0;
    }
    gAlxBarPhys = Bar;
    gAlxBar = (volatile UINT8 *)(UINTN)Bar;
    gAlxDid = Did;
    gAlxRev = Rev;

    if (!AlxLoadPermMac(Mac)) {
        LogDrv("Boot: Alx MAC Unavailable\n");
        gAlxBar = 0;
        return 0;
    }
    for (i = 0; i < 6; i++) {
        gAlxMac[i] = Mac[i];
    }
    LogMac(Mac);

    if (!AlxBringUp()) {
        LogNet("Boot: Alx BringUp Fail\n");
        gAlxBar = 0;
        gAlxDid = 0;
        return 0;
    }
    gAlxReady = 1;
    LogNet("Boot: Alx Ready Poll\n");
    return 1;
}

int AlxReady(void) {
    return gAlxReady;
}

void AlxGetMac(UINT8 Mac[6]) {
    int i;
    if (!Mac) {
        return;
    }
    for (i = 0; i < 6; i++) {
        Mac[i] = gAlxReady ? gAlxMac[i] : 0;
    }
}

UINT16 AlxPciDid(void) {
    return gAlxDid;
}
