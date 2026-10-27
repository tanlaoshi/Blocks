/*
 * Wifi.h — Pi 3B 板载 WiFi（Arasan SDIO / BCM43430；pwifi-1）
 */
#ifndef BOARD_WIFI_H
#define BOARD_WIFI_H

void BoardWifiRegister(void);
int BoardWifiClaim(void); /* 1=芯已探针；0=无 */
int BoardWifiReady(void); /* 同 Claim（本刀无关联栈） */

#endif
