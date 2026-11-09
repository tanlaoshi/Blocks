/*
 * WifiProbe.c — Duo SD1 SDIO 再探（wifi probe / wifiprobe）
 *
 * 自 Wifi.c 纯搬。板级日志经 WifiMarkBegin/End 指到 console。
 */
#include "WifiPrivate.h"

#if defined(__riscv)

static void ShellProbeMark(const char *Text) {
    gConsole.Write(Text);
}

/*
 * 板级日志出口：命令期间把 SdhciMark 指到 console，退出还原。
 * 原来这段 3 行在文件里重复了 20 次（10 组 begin/end），收成两个函数，行为不变。
 */
void WifiMarkBegin(void) {
    if (SdhciMarkSetQuiet != 0) {
        SdhciMarkSetQuiet(0); /* 壳调试要全量证据行 */
    }
    if (SdhciWifiMarkSink != 0) {
        SdhciWifiMarkSink(ShellProbeMark);
    }
}

void WifiMarkEnd(void) {
    if (SdhciWifiMarkSink != 0) {
        SdhciWifiMarkSink(0);
    }
    if (SdhciMarkSetQuiet != 0) {
        SdhciMarkSetQuiet(1); /* 回到冷启 brief */
    }
}

void CommandWifiProbe(int WithWake) {
    int Result;
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
    Result = SdhciWifiProbe(Flags);
    WifiMarkEnd();
    gConsole.Write(Result == 0 ? "wifi: SDIO probe ok\n"
                               : "wifi: SDIO probe FAILED (see above)\n");
}

/* wifiprobe：等价于 wifi probe，直接重跑 SD1 枚举（真机排障用） */
void CommandWifiProbeOnly(int Argc, char **Argv) {
    CommandWifiProbe(Argc >= 2 && Argv[1][0] == 'w' && Argv[1][1] == 'a');
}

#else
/* x86/arm64：无 Duo SDIO 探口；wifiprobe 不注册 */
#endif /* __riscv */
