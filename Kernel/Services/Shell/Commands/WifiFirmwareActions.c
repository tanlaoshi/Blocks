/*
 * WifiFirmwareActions.c — wififw bringup/scan/join/eapol 与解析辅助
 *
 * 自 Wifi.c 纯搬。
 */
#include "WifiPrivate.h"

#if defined(__riscv)

void WifiFwRun(UINT32 Mode) {
    WifiMarkBegin();
    {
        /* 评审 Y2：返回值不许丢，成败必须落到串口上 */
        int Result = SdhciWifiFw(Mode);
        gConsole.Write(Result == 0 ? "wififw: ok\n" : "wififw: FAILED\n");
    }
    WifiMarkEnd();
}

/*
 * wififw bringup [dry]：三步配置的唯一出口。
 * dry 只拼包、不需要卡；非 dry 由板级入口自己去查卡/查消息通道。
 */
void WifiFwBringup(int Dry) {
    if (SdhciWifiBringup == 0) {
        gConsole.Write("wifi: this board has no SDIO firmware stage\n");
        return;
    }
    WifiMarkBegin();
    gConsole.Write(SdhciWifiBringup(Dry) == 0 ? "wififw: ok\n" : "wififw: FAILED\n");
    WifiMarkEnd();
}

/* wififw scan [dry|verbose] [repeat N]：0=真扫、1=只拼包（不需要卡）、2=真扫并打原始字节 */
void WifiFwScan(int Mode, UINT32 Rounds) {
    if (SdhciWifiScan == 0) {
        gConsole.Write("wifi: this board has no SDIO firmware stage\n");
        return;
    }
    WifiMarkBegin();
    gConsole.Write(SdhciWifiScan(Mode, Rounds) == 0 ? "wififw: ok\n"
                                                    : "wififw: FAILED\n");
    WifiMarkEnd();
}

/* 解析十六进制（允许 0x 前缀）；成功返回 1 */
int WifiParseHex(const char *Text, UINT32 *Out) {
    UINT32 Value = 0;
    int Index;

    if (Text == 0 || Text[0] == 0) {
        return 0;
    }
    for (Index = 0; Text[Index] != 0; Index++) {
        char Character = Text[Index];
        UINT32 Digit;

        if (Character >= '0' && Character <= '9') {
            Digit = (UINT32)(Character - '0');
        } else if (Character >= 'a' && Character <= 'f') {
            Digit = (UINT32)(Character - 'a' + 10);
        } else if (Character >= 'A' && Character <= 'F') {
            Digit = (UINT32)(Character - 'A' + 10);
        } else if (Character == 'x' || Character == 'X') {
            continue;
        } else {
            return 0;
        }
        if (Value > 0x0FFFFFFFu) {
            return 0;
        }
        Value = (Value << 4) | Digit;
    }
    *Out = Value;
    return 1;
}

void CommandWifiFwScan(int Argc, char **Argv) {
    UINT32 ScanMode = 0;
    UINT32 Rounds = WIFI_SCAN_ROUNDS_DEFAULT;
    int Dry = 0;
    int Index = 2;

    if (Argc > 2 && ArgEqualIgnoreCase(Argv[2], "dry")) {
        Dry = 1;
        Index = 3;
    } else if (Argc > 2 && ArgEqualIgnoreCase(Argv[2], "verbose")) {
        ScanMode = WIFI_SCAN_VERBOSE;
        Index = 3;
    }
    if (Index < Argc) {
        if (ArgEqualIgnoreCase(Argv[Index], "repeat") == 0 || Index + 1 >= Argc ||
            WifiParseHex(Argv[Index + 1], &Rounds) == 0 || Rounds < 1u ||
            Rounds > WIFI_SCAN_ROUNDS_MAX || (Dry != 0 && Argc > 3)) {
            gConsole.Write("wififw scan [dry|verbose] [repeat 1..8]\n");
            return;
        }
        Index += 2;
    }
    if (Index != Argc) {
        gConsole.Write("wififw scan [dry|verbose] [repeat 1..8]\n");
        return;
    }
    WifiFwScan(Dry != 0 ? 1u : ScanMode, Rounds);
}

/*
 * wififw join [dry] <ssid> [<password>] [bssid <aa:bb:cc:dd:ee:ff>]
 *
 * 位置参数只有两个：ssid、password。**给了密码才做四次握手**（不给就到控制端口为止）。
 * bssid 改成带关键字的尾巴，因为位置上的第二参数现在归密码 —— 这样"aa:bb:.. 到底是
 * 密码还是 bssid"不用猜。
 * 返回值只决定 "wififw: ok/FAILED"；真正的判据是板级那几行（control port open /
 * eapol rx M1 / pbkdf2 / ptk / m2 / M3，文案各不相同）。
 */
static void WifiFwJoinUsage(void) {
    gConsole.Write(
        "wififw join [dry] <ssid> [<password>] [bssid <aa:bb:cc:dd:ee:ff>]\n");
}

/*
 * 密码用完清零。改的是 ConsoleRunLine 里 Buffer 的栈副本（Argv 指向它），
 * 清掉不影响本命令之外的东西；控制台的 gLine/历史缓冲不在本文件能碰的范围。
 */
static void WifiClearSecret(char *Text) {
    while (Text != 0 && *Text != 0) {
        *Text = 0;
        Text++;
    }
}

void CommandWifiFwJoin(int Argc, char **Argv) {
    const char *Ssid;
    const char *Password = 0;
    const char *Bssid = 0;
    int Dry = 0;
    int Index = 2;
    int Result;

    if (SdhciWifiJoin == 0) {
        gConsole.Write("wifi: this board has no SDIO firmware stage\n");
        return;
    }
    if (Argc > 2 && ArgEqualIgnoreCase(Argv[2], "dry") != 0) {
        Dry = 1;
        Index = 3;
    }
    if (Index >= Argc) {
        WifiFwJoinUsage();
        return;
    }
    Ssid = Argv[Index++];
    if (Index < Argc && ArgEqualIgnoreCase(Argv[Index], "bssid") == 0) {
        Password = Argv[Index++];
    }
    if (Index < Argc) {
        if (ArgEqualIgnoreCase(Argv[Index], "bssid") == 0 || Index + 1 >= Argc) {
            WifiFwJoinUsage();
            return;
        }
        Bssid = Argv[Index + 1];
        Index += 2;
    }
    if (Index != Argc) {
        WifiFwJoinUsage();
        return;
    }
    WifiMarkBegin();
    Result = SdhciWifiJoin(Dry, Ssid, Bssid, Password);
    gConsole.Write(Result == 0 ? "wififw: ok\n" : "wififw: FAILED\n");
    WifiMarkEnd();
    WifiClearSecret((char *)Password);
}

/* wififw eapol poll [ms]：join 之后盯一段数据帧，把 EAPOL(0x888e) 打出来。
 * ms 走十进制（"3000" = 3 秒）；判据是板级那行 eapol rx + msg=M1。 */
void CommandWifiFwEapol(int Argc, char **Argv) {
    if (SdhciWifiEapolPoll == 0) {
        gConsole.Write("wifi: this board has no SDIO firmware stage\n");
        return;
    }
    if (Argc < 3 || Argc > 4 || ArgEqualIgnoreCase(Argv[2], "poll") == 0) {
        gConsole.Write("wififw eapol poll [ms]\n");
        return;
    }
    WifiMarkBegin();
    gConsole.Write(SdhciWifiEapolPoll(Argc == 4 ? Argv[3] : 0) == 0
                       ? "wififw: ok\n"
                       : "wififw: FAILED\n");
    WifiMarkEnd();
}

#else
/* x86/arm64：无 wififw 子命令 */
#endif /* __riscv */
