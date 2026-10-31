/*
 * Wifi.c — 壳 wifi info|scan|join（pwifi-2）
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "HalDevices.h"

/*
 * Duo S 的 SD1（wifi-sd）SDIO 探测在板包里（HAL/RiscV/Board/milk-v-duo-s/Sdhci）。
 * 这里用弱引用：别的 RiscV 靶没有这些符号 → 链成 NULL，命令只提示"本板没有"；
 * x86/arm64 连这段都不编。加命令的代价就是一行 extern，不必为它动 HAL 头。
 */
#if defined(__riscv)
extern int SdhciWifiProbe(UINT32 Flags) __attribute__((weak));
extern UINT32 SdhciWifiProbeForceFlag(void) __attribute__((weak));
extern void SdhciWifiMarkSink(void (*Sink)(const char *Text)) __attribute__((weak));
extern int SdhciWifiFw(UINT32 Mode) __attribute__((weak));
extern int SdhciWifiFifoTest(void) __attribute__((weak));
extern int SdhciWifiFwRead(UINT32 Address) __attribute__((weak));
extern int SdhciWifiFwWriteDiag(UINT32 Address, UINT32 Value) __attribute__((weak));
extern int SdhciWifiFwVersion(void) __attribute__((weak));
extern int SdhciWifiFwMmReset(void) __attribute__((weak));
/* PR-31：三条配置消息（ME_CONFIG → ME_CHAN_CONFIG → MM_START）。Dry != 0 只拼包打印 */
extern int SdhciWifiBringup(int Dry) __attribute__((weak));
/* PR-31 第二刀：2=verbose 真扫+取证；0=真扫、1=dry；与板级 AIC_SCAN_MODE_* 同步 */
#define WIFI_SCAN_VERBOSE 2
#define WIFI_SCAN_ROUNDS_DEFAULT 3u
#define WIFI_SCAN_ROUNDS_MAX 8u
extern int SdhciWifiScan(int Mode, UINT32 Rounds) __attribute__((weak));
/* PR-32 第一刀：SM_CONNECT_REQ(0x1800) 发起关联。Dry != 0 只拼包（不需要卡） */
extern int SdhciWifiJoin(int Dry, const char *Ssid, const char *BssidText, const char *Password) __attribute__((weak));
/* PR-33：收 EAPOL（SDIO 数据帧 0x888e）。MsText=0 用板级默认预算（3000ms） */
extern int SdhciWifiEapolPoll(const char *MsText) __attribute__((weak));
/* PR-36：通用以太网 TX + DHCP 客户端。MsText=0 用板级默认预算（8000ms）；只读 */
extern int SdhciWifiDhcp(const char *MsText) __attribute__((weak));

/*
 * 板级模式位（与 Kernel/HAL/RiscV/Board/milk-v-duo-s/Sdhci.h 的 SDHCI_WIFI_FW_* 一致）。
 * Shell 层不 include 板级头，所以在这里镜像一份；改板级时务必同步。
 */
#define WIFI_FW_STAT 1u
#define WIFI_FW_ID 2u
#define WIFI_FW_WRITE 4u
#define WIFI_FW_CONFIRM 8u
#define WIFI_FW_PATCH_DRY 16u
#endif

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

#if defined(__riscv)
static void ShellProbeMark(const char *Text) {
    gConsole.Write(Text);
}

/*
 * 板级日志出口：命令期间把 SdhciMark 指到 console，退出还原。
 * 原来这段 3 行在文件里重复了 20 次（10 组 begin/end），收成两个函数，行为不变。
 */
static void WifiMarkBegin(void) {
    if (SdhciWifiMarkSink != 0) {
        SdhciWifiMarkSink(ShellProbeMark);
    }
}

static void WifiMarkEnd(void) {
    if (SdhciWifiMarkSink != 0) {
        SdhciWifiMarkSink(0);
    }
}

static void CommandWifiProbe(int WithWake) {
    int Rc;
    UINT32 Flags = 1u; /* SDHCI_WIFI_PROBE_FORCE */

    if (SdhciWifiProbe == 0) {
        gConsole.Write("wifi: this board has no SDIO/Sdhci board driver\n");
        return;
    }
    gConsole.Write("wifi: SD1 SDIO probe (re-run, state cleared)\n");
    if (WithWake != 0) {
        Flags |= 2u; /* SDHCI_WIFI_PROBE_WAKE：PE7 拉高（一次性实验） */
        gConsole.Write("wifi: experimental: drive GPIOE7 (wakeup) high\n");
    }
    WifiMarkBegin();
    Rc = SdhciWifiProbe(Flags);
    WifiMarkEnd();
    gConsole.Write(Rc == 0 ? "wifi: SDIO probe ok\n" : "wifi: SDIO probe FAILED (see above)\n");
}
#endif

static void CommandWifi(int Argc, char **Argv) {
    if (Argc >= 2 && ArgEqualIgnoreCase(Argv[1], "probe")) {
#if defined(__riscv)
        CommandWifiProbe(0);
#else
        gConsole.Write("wifi: probe only on duo-s\n");
#endif
        return;
    }
    if (Argc >= 2 && ArgEqualIgnoreCase(Argv[1], "scan")) { CommandWifiScan(); return; }
    if (Argc >= 3 && ArgEqualIgnoreCase(Argv[1], "join")) { CommandWifiJoin(Argv[2]); return; }
    if (Argc >= 2 && ArgEqualIgnoreCase(Argv[1], "info")) { CommandWifiInfo(); return; }
    if (Argc < 2) {
        CommandWifiInfo();
        gConsole.Write("hint: wifi probe - re-run SD1 (wifi-sd) SDIO probe\n");
        return;
    }
    gConsole.Write("wifi [info|scan|join <ssid>|probe]\n");
}

#if defined(__riscv)
/* wifiprobe：等价于 wifi probe，直接重跑 SD1 枚举（真机排障用） */
static void CommandWifiProbeOnly(int Argc, char **Argv) {
    CommandWifiProbe(Argc >= 2 && Argv[1][0] == 'w' && Argv[1][1] == 'a');
}
#endif

#if defined(__riscv)
/*
 * wififw：固件阶段（子命令全集见文件末尾的用法行）。要点：
 *   write / write confirm  真写不可逆，所以拆成"检查 / confirm"两段，防手滑；
 *   bringup / bringup dry  三步配置；dry 只拼包；
 *   scan / scan dry        MM_ADD_IF + SCANU_START 多轮合并；dry 只拼包；
 *   join / join dry        PR-32：SM_CONNECT_REQ(0x1800) → CFM(0x1801) → IND(0x1802)；
 *                          PR-34 起关联成功之后接着走控制端口 + 四次握手（给密码才做），
 *                          MM_KEY_ADD 仍不在这一刀。
 */
static void WifiFwRun(UINT32 Mode) {
    WifiMarkBegin();
    {
        /* 评审 Y2：返回值不许丢，成败必须落到串口上 */
        int Rc = SdhciWifiFw(Mode);
        gConsole.Write(Rc == 0 ? "wififw: ok\n" : "wififw: FAILED\n");
    }
    WifiMarkEnd();
}

/*
 * wififw bringup [dry]：三步配置的唯一出口。
 * dry 只拼包、不需要卡；非 dry 由板级入口自己去查卡/查消息通道。
 */
static void WifiFwBringup(int Dry) {
    if (SdhciWifiBringup == 0) {
        gConsole.Write("wifi: this board has no SDIO firmware stage\n");
        return;
    }
    WifiMarkBegin();
    gConsole.Write(SdhciWifiBringup(Dry) == 0 ? "wififw: ok\n" : "wififw: FAILED\n");
    WifiMarkEnd();
}

/* wififw scan [dry|verbose] [repeat N]：0=真扫、1=只拼包（不需要卡）、2=真扫并打原始字节 */
static void WifiFwScan(int Mode, UINT32 Rounds) {
    if (SdhciWifiScan == 0) {
        gConsole.Write("wifi: this board has no SDIO firmware stage\n");
        return;
    }
    WifiMarkBegin();
    gConsole.Write(SdhciWifiScan(Mode, Rounds) == 0 ? "wififw: ok\n" : "wififw: FAILED\n");
    WifiMarkEnd();
}

/* 解析十六进制（允许 0x 前缀）；成功返回 1 */
static int WifiParseHex(const char *Text, UINT32 *Out) {
    UINT32 V = 0;
    int k;
    if (Text == 0 || Text[0] == 0) {
        return 0;
    }
    for (k = 0; Text[k] != 0; k++) {
        char c = Text[k];
        UINT32 d;
        if (c >= '0' && c <= '9') {
            d = (UINT32)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            d = (UINT32)(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            d = (UINT32)(c - 'A' + 10);
        } else if (c == 'x' || c == 'X') {
            continue;
        } else {
            return 0;
        }
        if (V > 0x0FFFFFFFu) {
            return 0;
        }
        V = (V << 4) | d;
    }
    *Out = V;
    return 1;
}
static void CommandWifiFwScan(int Argc, char **Argv) {
    UINT32 ScanMode = 0, Rounds = WIFI_SCAN_ROUNDS_DEFAULT;
    int Dry = 0, Index = 2;
    if (Argc > 2 && ArgEqualIgnoreCase(Argv[2], "dry")) { Dry = 1; Index = 3; }
    else if (Argc > 2 && ArgEqualIgnoreCase(Argv[2], "verbose")) { ScanMode = WIFI_SCAN_VERBOSE; Index = 3; }
    if (Index < Argc) {
        if (ArgEqualIgnoreCase(Argv[Index], "repeat") == 0 || Index + 1 >= Argc ||
            WifiParseHex(Argv[Index + 1], &Rounds) == 0 || Rounds < 1u ||
            Rounds > WIFI_SCAN_ROUNDS_MAX || (Dry != 0 && Argc > 3)) {
            gConsole.Write("wififw scan [dry|verbose] [repeat 1..8]\n");
            return;
        }
        Index += 2;
    }
    if (Index != Argc) { gConsole.Write("wififw scan [dry|verbose] [repeat 1..8]\n"); return; }
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
    gConsole.Write("wififw join [dry] <ssid> [<password>] [bssid <aa:bb:cc:dd:ee:ff>]\n");
}

/*
 * 密码用完清零。改的是 ConsoleRunLine 里 Buf 的栈副本（Argv 指向它），
 * 清掉不影响本命令之外的东西；控制台的 gLine/历史缓冲不在本文件能碰的范围。
 */
static void WifiClearSecret(char *Text) {
    while (Text != 0 && *Text != 0) {
        *Text = 0;
        Text++;
    }
}

static void CommandWifiFwJoin(int Argc, char **Argv) {
    const char *Ssid;
    const char *Password = 0;
    const char *Bssid = 0;
    int Dry = 0;
    int Index = 2;
    int Rc;

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
    Rc = SdhciWifiJoin(Dry, Ssid, Bssid, Password);
    gConsole.Write(Rc == 0 ? "wififw: ok\n" : "wififw: FAILED\n");
    WifiMarkEnd();
    WifiClearSecret((char *)Password);
}

/* wififw eapol poll [ms]：join 之后盯一段数据帧，把 EAPOL(0x888e) 打出来。
 * ms 走十进制（"3000" = 3 秒）；判据是板级那行 eapol rx + msg=M1。 */
static void CommandWifiFwEapol(int Argc, char **Argv) {
    if (SdhciWifiEapolPoll == 0) { gConsole.Write("wifi: this board has no SDIO firmware stage\n"); return; }
    if (Argc < 3 || Argc > 4 || ArgEqualIgnoreCase(Argv[2], "poll") == 0) { gConsole.Write("wififw eapol poll [ms]\n"); return; }
    WifiMarkBegin();
    gConsole.Write(SdhciWifiEapolPoll(Argc == 4 ? Argv[3] : 0) == 0 ? "wififw: ok\n" : "wififw: FAILED\n");
    WifiMarkEnd();
}

/* 分发：把命令行翻成 Mode，然后**一次**调用 WifiFwRun；要么 return 于用法/自检，
 * 要么落在最后那一行（以前新增子命令忘了从 else-if 链里排除，会错打成用法行）。 */
static void CommandWifiFw(int Argc, char **Argv) {
    UINT32 Mode = 0u;
    if (SdhciWifiFw == 0) {
        gConsole.Write("wifi: this board has no SDIO firmware stage\n");
        return;
    }
    if (Argc < 2) {
        Mode = WIFI_FW_STAT; /* 裸命令：只 stat */
    } else if (ArgEqualIgnoreCase(Argv[1], "stat")) {
        Mode = WIFI_FW_STAT;
    } else if (ArgEqualIgnoreCase(Argv[1], "read")) {
        gConsole.Write("wififw: 'read' renamed to 'stat' (it only stats, never reads)\n");
        Mode = WIFI_FW_STAT;
    } else if (ArgEqualIgnoreCase(Argv[1], "id")) {
        Mode = WIFI_FW_ID;
    } else if (ArgEqualIgnoreCase(Argv[1], "write")) {
        Mode = WIFI_FW_WRITE;
        if (Argc >= 3 && ArgEqualIgnoreCase(Argv[2], "confirm")) {
            Mode |= WIFI_FW_CONFIRM;
        }
    } else if (ArgEqualIgnoreCase(Argv[1], "rd")) {
        /* 只读：读回一个 RAM 字，用于真写之后对账 */
        UINT32 Address = 0;
        int Bad = 0;
        int k;
        if (Argc < 3) {
            gConsole.Write("wififw rd <hexaddr>   (e.g. wififw rd 00201940)\n");
            return;
        }
        (void)k;
        if (WifiParseHex(Argv[2], &Address) == 0) {
            Bad = 1;
        }
        if (Bad != 0 || Address == 0u) {
            gConsole.Write("wififw rd: bad hex address (need 4-aligned, e.g. 00201940)\n");
            return;
        }
        if (SdhciWifiFwRead == 0) {
            gConsole.Write("wifi: this board has no SDIO firmware stage\n");
            return;
        }
        WifiMarkBegin();
        gConsole.Write(SdhciWifiFwRead(Address) == 0 ? "wififw: ok\n" : "wififw: FAILED\n");
        WifiMarkEnd();
        return;
    } else if (ArgEqualIgnoreCase(Argv[1], "mmreset")) {
        /* 只读诊断（复位 + 复查版本），不改 patch 状态 */
        if (SdhciWifiFwMmReset == 0) {
            gConsole.Write("wifi: this board has no SDIO firmware stage\n");
            return;
        }
        WifiMarkBegin();
        gConsole.Write(SdhciWifiFwMmReset() == 0 ? "wififw: ok\n" : "wififw: FAILED\n");
        WifiMarkEnd();
        return;
    } else if (ArgEqualIgnoreCase(Argv[1], "bringup")) {
        /* PR-31：三步配置。只认 "dry" 一个后缀，多打字就回用法，不猜 */
        if (Argc >= 3 && !ArgEqualIgnoreCase(Argv[2], "dry")) {
            gConsole.Write("wififw bringup [dry]\n");
            return;
        }
        WifiFwBringup(Argc >= 3 ? 1 : 0);
        return;
    } else if (ArgEqualIgnoreCase(Argv[1], "scan")) {
        CommandWifiFwScan(Argc, Argv);
        return;
    } else if (ArgEqualIgnoreCase(Argv[1], "join")) {
        /* PR-32：SM_CONNECT。放在一长串 else-if 里单独一个函数，别让本函数再长 */
        CommandWifiFwJoin(Argc, Argv);
        return;
    } else if (ArgEqualIgnoreCase(Argv[1], "eapol")) { CommandWifiFwEapol(Argc, Argv); return;
    } else if (ArgEqualIgnoreCase(Argv[1], "dhcp")) {
        /* PR-36：数据通路 + DHCP（只读、可重复）。ms 默认 8000，见板级 SdhciWifiDhcp */
        if (SdhciWifiDhcp == 0 || Argc > 3) {
            gConsole.Write("wififw dhcp [ms]\n");
            return;
        }
        WifiMarkBegin();
        gConsole.Write(SdhciWifiDhcp(Argc == 3 ? Argv[2] : 0) == 0 ? "wififw: ok\n" : "wififw: FAILED\n");
        WifiMarkEnd();
        return;
    } else if (ArgEqualIgnoreCase(Argv[1], "ver")) {
        /* 只读：MM_VERSION_REQ → 有 CFM 就证明固件在跑 */
        if (SdhciWifiFwVersion == 0) {
            gConsole.Write("wifi: this board has no SDIO firmware stage\n");
            return;
        }
        WifiMarkBegin();
        gConsole.Write(SdhciWifiFwVersion() == 0 ? "wififw: ok\n" : "wififw: FAILED\n");
        WifiMarkEnd();
        return;
    } else if (ArgEqualIgnoreCase(Argv[1], "wr")) {
        /* 诊断：单字写 + 立刻读回（同一上电周期内才有意义） */
        UINT32 Address = 0;
        UINT32 Value = 0;
        if (Argc < 4) {
            gConsole.Write("wififw wr <hexaddr> <hexval>  (e.g. wififw wr 00201940 000ee5fd)\n");
            return;
        }
        if (WifiParseHex(Argv[2], &Address) == 0 || WifiParseHex(Argv[3], &Value) == 0) {
            gConsole.Write("wififw wr: bad hex addr/val\n");
            return;
        }
        if (SdhciWifiFwWriteDiag == 0) {
            gConsole.Write("wifi: this board has no SDIO firmware stage\n");
            return;
        }
        WifiMarkBegin();
        gConsole.Write(SdhciWifiFwWriteDiag(Address, Value) == 0 ? "wififw: ok\n" : "wififw: FAILED\n");
        WifiMarkEnd();
        return;
    } else if (ArgEqualIgnoreCase(Argv[1], "patch")) {
        if (Argc < 3) {
            gConsole.Write("wififw patch dry\n");
            return;
        }
        if (ArgEqualIgnoreCase(Argv[2], "dry")) {
            Mode = WIFI_FW_PATCH_DRY;
        } else if (ArgEqualIgnoreCase(Argv[2], "apply")) {
            /* 评审 P0-1：独立 apply 会绕过"固件已驻留"前置，已删除 */
            gConsole.Write("wififw patch apply: removed (it could write with no firmware in RAM)\n");
            gConsole.Write("use 'wififw write confirm' -- patch table is now stage 4/5\n");
            return;
        } else {
            gConsole.Write("wififw patch dry\n");
            return;
        }
    } else if (ArgEqualIgnoreCase(Argv[1], "fifotest")) {
        /* 显式 FIFO 回环自检：只做人工诊断，不参与枚举判定 */
        if (SdhciWifiFifoTest == 0) {
            gConsole.Write("wifi: this board has no FIFO self-test\n");
            return;
        }
        WifiMarkBegin();
        (void)SdhciWifiFifoTest();
        WifiMarkEnd();
        return;
    } else {
        gConsole.Write("wififw [stat|id|rd <addr>|wr <addr> <val>|ver|mmreset|bringup [dry]|scan [dry|verbose] [repeat 1..8]|join [dry] <ssid> [password] [bssid <addr>]|eapol poll [ms]|dhcp [ms]|write [confirm]|fifotest|patch dry]\n");
        return;
    }
    WifiFwRun(Mode);
}
#endif
/*
 * 无桌面/无 FB 的板子（Duo S）走 ShellCommandsRegisterVirtMin，只挂命令最小集，
 * 所以 wifiprobe 单独开一个注册口给那里用；桌面靶则由 ShellCommandsWifiRegister 一起挂。
 */
void ShellCommandsWifiProbeRegister(void) {
#if defined(__riscv)
    gConsole.Register("wifiprobe", "re-run SD1 (wifi-sd) SDIO probe; wifiprobe wake = also drive PE7",
                      CommandWifiProbeOnly);
    gConsole.Register("wififw", "firmware stage: wififw [stat|id|rd <addr>|wr <addr> <val>|ver|mmreset|bringup [dry]|scan [dry|verbose] [repeat 1..8]|join [dry] <ssid> [password] [bssid <addr>]|eapol poll [ms]|dhcp [ms]|write [confirm]|fifotest|patch dry]",
                      CommandWifiFw);
#endif
}
void ShellCommandsWifiRegister(void) {
    gConsole.Register("wifi", "wifi [info|scan|join <ssid>|probe]", CommandWifi);
    ShellCommandsWifiProbeRegister();
}
