/*
 * WifiNetif.h — Duo WiFi 挂进通用网的板级接口（对表 Pi 的 Wifi.h）
 *
 * 只在 Duo 板内使用；LwIp/Netif.c 通过它挂 TryLinkOutput 钩子。
 * 语义与 Pi 那三个同名符号一致，便于评审对照：
 *   Arm(StationMac)  join+装钥成功后武装
 *   TryLinkOutput    武装时交出帧；未武装/失败返回 -1（调用方回退 dwmac）
 */
#ifndef WIFI_NETIF_H
#define WIFI_NETIF_H

#include "BootTypes.h"

#define WIFI_NETIF_MAC_LENGTH 6u

void BoardWifiArmNetifLinkOutput(const UINT8 StationMac[WIFI_NETIF_MAC_LENGTH]);
void BoardWifiDisarmNetifLinkOutput(void);
int  BoardWifiLinkOutputIsArmed(void);
int  BoardWifiCopyStationMacIfArmed(UINT8 OutMac[WIFI_NETIF_MAC_LENGTH]);
int  BoardWifiTryLinkOutput(const UINT8 *Frame, UINTN FrameLength);
UINT32 BoardWifiLinkOutputCount(void);
UINT32 BoardWifiLinkOutputFailCount(void);

/* 从 join 现状武装（壳命令 / netif-3 自动武装共用） */
int BoardWifiArmNetifFromJoin(void);

/* RX：武装后每拍短喂（对表 Pi BoardWifiPollFeedLwIp）；返回本拍喂入的包数 */
int BoardWifiPollFeedLwIp(UINT32 MaxPackets);
UINT32 BoardWifiFeedCount(void);
UINT32 BoardWifiFeedDropCount(void);
void BoardWifiFeedReset(void);
void BoardWifiFeedAttachSink(int Enable);

#endif
