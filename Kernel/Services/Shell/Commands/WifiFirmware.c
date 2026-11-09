/*
 * WifiFirmware.c — 壳 wififw 分发（Duo 固件阶段）
 *
 * 自 Wifi.c 纯搬。子命令实现见 WifiFirmwareActions.c。
 */
#include "WifiPrivate.h"

#if defined(__riscv)

/*
 * wififw：固件阶段（子命令全集见用法行）。要点：
 *   write / write confirm  真写不可逆，所以拆成"检查 / confirm"两段，防手滑；
 *   bringup / bringup dry  三步配置；dry 只拼包；
 *   scan / scan dry        MM_ADD_IF + SCANU_START 多轮合并；dry 只拼包；
 *   join / join dry        PR-32：SM_CONNECT_REQ(0x1800) → CFM(0x1801) → IND(0x1802)；
 *                          PR-34 起关联成功之后接着走控制端口 + 四次握手（给密码才做），
 *                          MM_KEY_ADD 仍不在这一刀。
 */
/* 分发：把命令行翻成 Mode，然后**一次**调用 WifiFwRun；要么 return 于用法/自检，
 * 要么落在最后那一行（以前新增子命令忘了从 else-if 链里排除，会错打成用法行）。 */
void CommandWifiFw(int Argc, char **Argv) {
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

        if (Argc < 3) {
            gConsole.Write("wififw rd <hexaddr>   (e.g. wififw rd 00201940)\n");
            return;
        }
        if (WifiParseHex(Argv[2], &Address) == 0) {
            Bad = 1;
        }
        if (Bad != 0 || Address == 0u) {
            gConsole.Write(
                "wififw rd: bad hex address (need 4-aligned, e.g. 00201940)\n");
            return;
        }
        if (SdhciWifiFwRead == 0) {
            gConsole.Write("wifi: this board has no SDIO firmware stage\n");
            return;
        }
        WifiMarkBegin();
        gConsole.Write(SdhciWifiFwRead(Address) == 0 ? "wififw: ok\n"
                                                     : "wififw: FAILED\n");
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
    } else if (ArgEqualIgnoreCase(Argv[1], "eapol")) {
        CommandWifiFwEapol(Argc, Argv);
        return;
    } else if (ArgEqualIgnoreCase(Argv[1], "dhcp")) {
        /* PR-36：数据通路 + DHCP（只读、可重复）。ms 默认 8000，见板级 SdhciWifiDhcp */
        if (SdhciWifiDhcp == 0 || Argc > 3) {
            gConsole.Write("wififw dhcp [ms]\n");
            return;
        }
        WifiMarkBegin();
        gConsole.Write(SdhciWifiDhcp(Argc == 3 ? Argv[2] : 0) == 0 ? "wififw: ok\n"
                                                                   : "wififw: FAILED\n");
        WifiMarkEnd();
        return;
    } else if (ArgEqualIgnoreCase(Argv[1], "ping")) {
        if (SdhciWifiPing == 0 || Argc > 3) {
            gConsole.Write("wififw ping [ms]\n");
            return;
        }
        WifiMarkBegin();
        gConsole.Write(SdhciWifiPing(Argc == 3 ? Argv[2] : 0) == 0 ? "wififw: ok\n"
                                                                   : "wififw: FAILED\n");
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
            gConsole.Write(
                "wififw wr <hexaddr> <hexval>  (e.g. wififw wr 00201940 000ee5fd)\n");
            return;
        }
        if (WifiParseHex(Argv[2], &Address) == 0 ||
            WifiParseHex(Argv[3], &Value) == 0) {
            gConsole.Write("wififw wr: bad hex addr/val\n");
            return;
        }
        if (SdhciWifiFwWriteDiag == 0) {
            gConsole.Write("wifi: this board has no SDIO firmware stage\n");
            return;
        }
        WifiMarkBegin();
        gConsole.Write(SdhciWifiFwWriteDiag(Address, Value) == 0 ? "wififw: ok\n"
                                                                 : "wififw: FAILED\n");
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
            gConsole.Write(
                "wififw patch apply: removed (it could write with no firmware in RAM)\n");
            gConsole.Write(
                "use 'wififw write confirm' -- patch table is now stage 4/5\n");
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
        gConsole.Write(
            "wififw [stat|id|rd <addr>|wr <addr> <val>|ver|mmreset|bringup [dry]|scan "
            "[dry|verbose] [repeat 1..8]|join [dry] <ssid> [password] [bssid "
            "<addr>]|eapol poll [ms]|dhcp [ms]|write [confirm]|fifotest|patch dry]\n");
        return;
    }
    WifiFwRun(Mode);
}

#else
/* x86/arm64：无 wififw */
#endif /* __riscv */
