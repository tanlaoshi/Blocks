/*
 * WifiPrivate.h — 壳 wifi / wifiprobe / wififw 内部分发
 *
 * 板符号 weak：virt CI 无 Duo 实现时链成 0。勿 include 板级 Sdhci.h。
 */
#ifndef WIFI_PRIVATE_H
#define WIFI_PRIVATE_H

#include "ShellPrivate.h"
#include "Console.h"
#include "HalDevices.h"

#if defined(__riscv)
extern int SdhciWifiProbe(UINT32 Flags) __attribute__((weak));
extern UINT32 SdhciWifiProbeForceFlag(void) __attribute__((weak));
extern void SdhciWifiMarkSink(void (*Sink)(const char *Text)) __attribute__((weak));
extern void SdhciMarkSetQuiet(int Quiet) __attribute__((weak));
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
extern int SdhciWifiJoin(int Dry, const char *Ssid, const char *BssidText,
                         const char *Password) __attribute__((weak));
/* PR-33：收 EAPOL（SDIO 数据帧 0x888e）。MsText=0 用板级默认预算（3000ms） */
extern int SdhciWifiEapolPoll(const char *MsText) __attribute__((weak));
/* PR-36：通用以太网 TX + DHCP 客户端。MsText=0 用板级默认预算（8000ms）；只读 */
extern int SdhciWifiDhcp(const char *MsText) __attribute__((weak));
/* dwifi-ping：对 DHCP 网关 ICMP；MsText=0 默认 4000ms */
extern int SdhciWifiPing(const char *MsText) __attribute__((weak));

/*
 * 板级模式位（与 Kernel/HAL/RiscV/Board/milk-v-duo-s/Sdhci.h 的 SDHCI_WIFI_FW_* 一致）。
 * Shell 层不 include 板级头，所以在这里镜像一份；改板级时务必同步。
 */
#define WIFI_FW_STAT 1u
#define WIFI_FW_ID 2u
#define WIFI_FW_WRITE 4u
#define WIFI_FW_CONFIRM 8u
#define WIFI_FW_PATCH_DRY 16u

void WifiMarkBegin(void);
void WifiMarkEnd(void);
void CommandWifiProbe(int WithWake);
void CommandWifiProbeOnly(int Argc, char **Argv);
void CommandWifiFw(int Argc, char **Argv);
int WifiParseHex(const char *Text, UINT32 *Out);
void WifiFwRun(UINT32 Mode);
void WifiFwBringup(int Dry);
void WifiFwScan(int Mode, UINT32 Rounds);
void CommandWifiFwScan(int Argc, char **Argv);
void CommandWifiFwJoin(int Argc, char **Argv);
void CommandWifiFwEapol(int Argc, char **Argv);
#endif /* __riscv */

static inline int ArgEqualIgnoreCase(const char *Arg, const char *Word) {
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

void WifiWriteDecimal(INT32 Value);

#endif /* WIFI_PRIVATE_H */
