/*
 * Wifi.c — 壳 wifi info|scan|join|probe 分发（pwifi-2）
 *
 * Duo SDIO 探测 / wififw：WifiProbe.c / WifiFirmware*.c。
 * wifinet 另见 WifiNetif.c。
 */
#include "WifiPrivate.h"

void WifiWriteDecimal(INT32 Value) {
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
        WifiWriteDecimal((INT32)Channel);
        gConsole.Write(" rssi=");
        WifiWriteDecimal(Rssi);
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
    if (Argc >= 2 && ArgEqualIgnoreCase(Argv[1], "probe")) {
#if defined(__riscv)
        CommandWifiProbe(0);
#else
        gConsole.Write("wifi: probe only on duo-s\n");
#endif
        return;
    }
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
        gConsole.Write("hint: wifi probe - re-run SD1 (wifi-sd) SDIO probe\n");
        return;
    }
    gConsole.Write("wifi [info|scan|join <ssid>|probe]\n");
}

/*
 * 无桌面/无 FB 的板子（Duo S）走 ShellCommandsRegisterVirtMin，只挂命令最小集，
 * 所以 wifiprobe 单独开一个注册口给那里用；桌面靶则由 ShellCommandsWifiRegister 一起挂。
 */
void ShellCommandsWifiProbeRegister(void) {
#if defined(__riscv)
    gConsole.Register("wifiprobe",
                      "re-run SD1 (wifi-sd) SDIO probe; wifiprobe wake = also drive PE7",
                      CommandWifiProbeOnly);
    gConsole.Register(
        "wififw",
        "firmware stage: wififw [stat|id|rd <addr>|wr <addr> <val>|ver|mmreset|bringup "
        "[dry]|scan [dry|verbose] [repeat 1..8]|join [dry] <ssid> [password] [bssid "
        "<addr>]|eapol poll [ms]|dhcp [ms]|write [confirm]|fifotest|patch dry]",
        CommandWifiFw);
#endif
}

void ShellCommandsWifiRegister(void) {
    gConsole.Register("wifi", "wifi [info|scan|join <ssid>|probe]", CommandWifi);
    ShellCommandsWifiProbeRegister();
}
