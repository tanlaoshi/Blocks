/*
 * Wifi.h — Pi 3B 板载 WiFi（Arasan SDIO / BCM43430；pwifi-1/2）
 */
#ifndef BOARD_WIFI_H
#define BOARD_WIFI_H

#include "Abi/BootTypes.h"

#define BOARD_WIFI_SSID_MAXIMUM 32u

typedef struct {
    UINT8 SsidLength;
    char Ssid[BOARD_WIFI_SSID_MAXIMUM + 1u];
    INT32 SignalRssi;
    UINT16 Channel;
} BOARD_WIFI_SCAN_ENTRY;

void BoardWifiRegister(void);
int BoardWifiClaim(void);  /* 1=有芯；尝试灌片 */
int BoardWifiReady(void);  /* 1=固件已灌 */
int BoardWifiScan(void);   /* 0=ok；填扫描表 */
int BoardWifiAssociateOpen(const char *Ssid);
UINT32 BoardWifiScanEntryCount(void);
const BOARD_WIFI_SCAN_ENTRY *BoardWifiScanEntryAt(UINT32 Index);
/* 7c3/7d：join 后武装；netif linkoutput 优先 SDPCM，未武装返回 -1 */
void BoardWifiArmNetifLinkOutput(const UINT8 StationMac[6]);
int BoardWifiLinkOutputIsArmed(void);
int BoardWifiCopyStationMacIfArmed(UINT8 Mac[6]); /* 0=已拷 */
int BoardWifiTryLinkOutput(const UINT8 *Frame, UINTN FrameLength);
UINT32 BoardWifiNetifLinkOutputCount(void);
void BoardWifiPollFeedLwIp(UINT32 MaxPackets);
/* 0=ok；-1 未武装；-2 LwIp 未起/netif down；-3 dhcp Restart 败 */
int BoardWifiKickDhcpDiscoverViaNetif(void);

#endif
