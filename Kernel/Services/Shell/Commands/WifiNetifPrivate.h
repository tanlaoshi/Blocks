/*
 * WifiNetifPrivate.h — wifinet 命令内部（Actions 与分发共用）
 *
 * 板符号 weak：virt CI 无 Duo 实现时链接为 0，运行时 WifiNetBoardPresent 拒掉。
 */
#ifndef WIFI_NETIF_PRIVATE_H
#define WIFI_NETIF_PRIVATE_H

#include "Hal.h"

#if defined(__riscv)

#define WIFI_NETIF_MAC_LENGTH 6u

extern void BoardWifiDisarmNetifLinkOutput(void) __attribute__((weak));
extern int  BoardWifiLinkOutputIsArmed(void) __attribute__((weak));
extern int  BoardWifiCopyStationMacIfArmed(UINT8 OutMac[WIFI_NETIF_MAC_LENGTH])
    __attribute__((weak));
extern int  BoardWifiTryLinkOutput(const UINT8 *Frame, UINTN FrameLength)
    __attribute__((weak));
extern UINT32 BoardWifiLinkOutputCount(void) __attribute__((weak));
extern UINT32 BoardWifiLinkOutputFailCount(void) __attribute__((weak));
extern int  BoardWifiArmNetifFromJoin(void) __attribute__((weak));
extern int  BoardWifiPollFeedLwIp(UINT32 MaxPackets) __attribute__((weak));
extern UINT32 BoardWifiFeedCount(void) __attribute__((weak));
extern UINT32 BoardWifiFeedDropCount(void) __attribute__((weak));
extern UINT32 BoardWifiFeedPolls(void) __attribute__((weak));
extern UINT32 BoardWifiFeedRawFrames(void) __attribute__((weak));
extern UINT32 BoardWifiFeedSinkCalls(void) __attribute__((weak));

int WifiNetBoardPresent(void);
int WifiNetParseInteger(const char *Text, UINT32 *Out);
int WifiNetParseIpv4(const char *Text, UINT32 *OutIp);
void WifiNetWriteDecimal(UINT32 Value);
void WifiNetPrintMac(void);
void WifiNetAttach(void);
void WifiNetDetach(void);
void WifiNetPump(UINT32 Milliseconds);
void WifiNetTransmit(UINT32 TargetIp);

#endif /* __riscv */

#endif /* WIFI_NETIF_PRIVATE_H */
