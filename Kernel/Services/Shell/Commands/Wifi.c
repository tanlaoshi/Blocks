/*
 * Wifi.c — 壳 wifi info|scan|join（pwifi-2）
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "HalDevices.h"

/*
 * Duo S 的 SD1（wifi-sd）SDIO 探测在板包里（HAL/RiscV/Board/milk-v-duo-s/Sdhci）。
 * 这里用弱引用：
 *   - RiscV 上的其它靶（virt）没有这个符号 → 链成 NULL，命令只提示"本板没有"；
 *   - x86/arm64 连这段都不编。
 * 加命令的代价就是这两行，不必为它动 HAL 头。
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
    if (SdhciWifiMarkSink != 0) {
        SdhciWifiMarkSink(ShellProbeMark);
    }
    Rc = SdhciWifiProbe(Flags);
    if (SdhciWifiMarkSink != 0) {
        SdhciWifiMarkSink(0);
    }
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

#if defined(__riscv)
/* wifiprobe：等价于 wifi probe，直接重跑 SD1 枚举（真机排障用） */
static void CommandWifiProbeOnly(int Argc, char **Argv) {
    int Wake = 0;

    if (Argc >= 2 && Argv[1][0] == 'w' && Argv[1][1] == 'a') {
        Wake = 1;
    }
    CommandWifiProbe(Wake);
}
#endif

#if defined(__riscv)
/*
 * wififw：S5b 固件阶段。参数：
 *   wififw stat     只读：只 stat 三个固件文件并打字节数（**不读内容、不碰芯片**）
 *   wififw read     旧名，等同 stat，会多打一行提示
 *   wififw id       只读：读芯片版本 0x40500000，顺带证明消息通道
 *   wififw write            **检查模式**：打印将要写的阶段/文件/大小/落点，一个字节都不写
 *   wififw write confirm    **真写**：adid → patch(可选) → fmacfw → start_app → 等首条消息
 *   wififw fifotest  只读：显式 FIFO 回环自检（不参与枚举判定）
 *   wififw patch dry    只解析 patch 表并打印全部待写条目，一个字节不写、不发消息
 *   wififw patch apply  真写 patch 表（每条 mem write 等 CFM，失败即停）
 *   wififw bringup      发 ME_CONFIG → ME_CHAN_CONFIG → MM_START，每步等 CFM（2000 ms）
 *   wififw bringup dry  只拼包：打印三条消息的总长与前 64 B，一个字节都不发
 *
 * 真写是不可逆操作，所以拆成"检查 / confirm"两段，防手滑。
 */
static void WifiFwRun(UINT32 Mode) {
    if (SdhciWifiMarkSink != 0) {
        SdhciWifiMarkSink(ShellProbeMark);
    }
    {
        /* 评审 Y2：返回值不许丢，成败必须落到串口上 */
        int Rc = SdhciWifiFw(Mode);

        gConsole.Write(Rc == 0 ? "wififw: ok\n" : "wififw: FAILED\n");
    }
    if (SdhciWifiMarkSink != 0) {
        SdhciWifiMarkSink(0);
    }
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
    if (SdhciWifiMarkSink != 0) {
        SdhciWifiMarkSink(ShellProbeMark);
    }
    gConsole.Write(SdhciWifiBringup(Dry) == 0 ? "wififw: ok\n" : "wififw: FAILED\n");
    if (SdhciWifiMarkSink != 0) {
        SdhciWifiMarkSink(0);
    }
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

/*
 * 分发只做一件事：把命令行翻成 Mode，然后**一次**调用 WifiFwRun。
 * 以前是"一长串 else-if + 末尾兜底"，新增 patch 子命令时忘了从链里排除，
 * 结果 `wififw patch dry` 落到兜底分支打出用法行（真机 11:15 复现）。
 * 现在只有一个出口：要么 return 于用法/自检，要么落在最后那一行。
 */
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
        if (SdhciWifiMarkSink != 0) {
            SdhciWifiMarkSink(ShellProbeMark);
        }
        gConsole.Write(SdhciWifiFwRead(Address) == 0 ? "wififw: ok\n" : "wififw: FAILED\n");
        if (SdhciWifiMarkSink != 0) {
            SdhciWifiMarkSink(0);
        }
        return;
    } else if (ArgEqualIgnoreCase(Argv[1], "mmreset")) {
        /* 只读诊断（复位 + 复查版本），不改 patch 状态 */
        if (SdhciWifiFwMmReset == 0) {
            gConsole.Write("wifi: this board has no SDIO firmware stage\n");
            return;
        }
        if (SdhciWifiMarkSink != 0) {
            SdhciWifiMarkSink(ShellProbeMark);
        }
        gConsole.Write(SdhciWifiFwMmReset() == 0 ? "wififw: ok\n" : "wififw: FAILED\n");
        if (SdhciWifiMarkSink != 0) {
            SdhciWifiMarkSink(0);
        }
        return;
    } else if (ArgEqualIgnoreCase(Argv[1], "bringup")) {
        /* PR-31：三步配置。只认 "dry" 一个后缀，多打字就回用法，不猜 */
        if (Argc >= 3 && !ArgEqualIgnoreCase(Argv[2], "dry")) {
            gConsole.Write("wififw bringup [dry]\n");
            return;
        }
        WifiFwBringup(Argc >= 3 ? 1 : 0);
        return;
    } else if (ArgEqualIgnoreCase(Argv[1], "ver")) {
        /* 只读：MM_VERSION_REQ → 有 CFM 就证明固件在跑 */
        if (SdhciWifiFwVersion == 0) {
            gConsole.Write("wifi: this board has no SDIO firmware stage\n");
            return;
        }
        if (SdhciWifiMarkSink != 0) {
            SdhciWifiMarkSink(ShellProbeMark);
        }
        gConsole.Write(SdhciWifiFwVersion() == 0 ? "wififw: ok\n" : "wififw: FAILED\n");
        if (SdhciWifiMarkSink != 0) {
            SdhciWifiMarkSink(0);
        }
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
        if (SdhciWifiMarkSink != 0) {
            SdhciWifiMarkSink(ShellProbeMark);
        }
        gConsole.Write(SdhciWifiFwWriteDiag(Address, Value) == 0 ? "wififw: ok\n" : "wififw: FAILED\n");
        if (SdhciWifiMarkSink != 0) {
            SdhciWifiMarkSink(0);
        }
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
        if (SdhciWifiMarkSink != 0) {
            SdhciWifiMarkSink(ShellProbeMark);
        }
        (void)SdhciWifiFifoTest();
        if (SdhciWifiMarkSink != 0) {
            SdhciWifiMarkSink(0);
        }
        return;
    } else {
        gConsole.Write("wififw [stat|id|rd <addr>|wr <addr> <val>|ver|mmreset|bringup [dry]|write [confirm]|fifotest|patch dry]\n");
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
    gConsole.Register("wififw", "firmware stage: wififw [stat|id|rd <addr>|wr <addr> <val>|ver|mmreset|bringup [dry]|write [confirm]|fifotest|patch dry]",
                      CommandWifiFw);
#endif
}

void ShellCommandsWifiRegister(void) {
    gConsole.Register("wifi", "wifi [info|scan|join <ssid>|probe]", CommandWifi);
    ShellCommandsWifiProbeRegister();
}
