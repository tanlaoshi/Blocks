/*
 * WifiIds.h — RTL8188EU VID/PID（控制器认棒用，不拉 Rtl8188eu.h）
 */
#ifndef WIFI_IDS_H
#define WIFI_IDS_H

#include "BootTypes.h"

#define WIFI_RTL_VID       0x0BDAu
#define WIFI_PID_8188EUS   0x8179u
#define WIFI_PID_8188ETV   0x0179u
#define WIFI_PID_8188EU_A  0x817Fu
#define WIFI_PID_8188EU_B  0x818Bu

int WifiIs8188EuPid(UINT16 Pid);

#endif
