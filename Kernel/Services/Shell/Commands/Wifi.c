/*
 * Wifi.c — 壳 wifi info|scan|join（pwifi-2）
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "HalDevices.h"

static int ArgEqualIgnoreCase(const char *Arg, const char *Word) {
    const char *A = Word;
    const char *B = Arg;

    if (!B) {
        return 0;
    }
    while (*A && *B) {
        char Ca = *A;
        char Cb = *B;

        if (Ca >= 'A' && Ca <= 'Z') {
            Ca = (char)(Ca - 'A' + 'a');
        }
        if (Cb >= 'A' && Cb <= 'Z') {
            Cb = (char)(Cb - 'A' + 'a');
        }
        if (Ca != Cb) {
            return 0;
        }
        A++;
        B++;
    }
    return (*A == 0 && *B == 0) ? 1 : 0;
}

static void WriteDecimal(INT32 Value) {
    char Digits[12];
    int Digit = 0;
    UINT32 Unsigned;

    if (Value < 0) {
        gConsole.Write("-");
        Unsigned = (UINT32)(-Value);
    } else {
        Unsigned = (UINT32)Value;
    }
    if (Unsigned == 0) {
        gConsole.Write("0");
        return;
    }
    while (Unsigned > 0 && Digit < 11) {
        Digits[Digit++] = (char)('0' + (Unsigned % 10u));
        Unsigned /= 10u;
    }
    while (Digit > 0) {
        char One[2];
        One[0] = Digits[--Digit];
        One[1] = 0;
        gConsole.Write(One);
    }
}

static void CommandWifiInfo(void) {
    (void)HalWifiClaim();
    gConsole.Write("wifi: ");
    if (!HalWifiReady()) {
        gConsole.Write("not ready (no chip/firmware)\n");
        return;
    }
    gConsole.Write("ready\n");
}

static void CommandWifiScan(void) {
    UINT32 Index;
    UINT32 Count;

    (void)HalWifiClaim();
    gConsole.Write("wifi: scanning...\n");
    if (HalWifiScan() != 0) {
        gConsole.Write("wifi: scan fail\n");
        return;
    }
    Count = HalWifiScanCount();
    if (Count == 0) {
        gConsole.Write("wifi: no AP\n");
        return;
    }
    for (Index = 0; Index < Count; Index++) {
        char Ssid[40];
        INT32 Rssi = 0;
        UINT16 Channel = 0;

        if (HalWifiScanEntryAt(Index, Ssid, sizeof(Ssid), &Rssi, &Channel) != 0) {
            continue;
        }
        gConsole.Write("  ");
        gConsole.Write(Ssid);
        gConsole.Write(" ch=");
        WriteDecimal((INT32)Channel);
        gConsole.Write(" rssi=");
        WriteDecimal(Rssi);
        gConsole.Write("\n");
    }
}

static void CommandWifiJoin(const char *Ssid) {
    (void)HalWifiClaim();
    if (HalWifiAssociateOpen(Ssid) != 0) {
        gConsole.Write("wifi: join fail (open only this knife)\n");
        return;
    }
    gConsole.Write("wifi: join sent (open)\n");
}

static void CommandWifi(int Argc, char **Argv) {
    if (Argc >= 2 && ArgEqualIgnoreCase(Argv[1], "scan")) {
        CommandWifiScan();
        return;
    }
    if (Argc >= 3 && ArgEqualIgnoreCase(Argv[1], "join")) {
        CommandWifiJoin(Argv[2]);
        return;
    }
    if (Argc >= 2 && ArgEqualIgnoreCase(Argv[1], "info")) {
        CommandWifiInfo();
        return;
    }
    if (Argc < 2) {
        CommandWifiInfo();
        return;
    }
    gConsole.Write("wifi [info|scan|join <ssid>]\n");
}

void ShellCommandsWifiRegister(void) {
    gConsole.Register("wifi", "wifi [info|scan|join <ssid>]", CommandWifi);
}
