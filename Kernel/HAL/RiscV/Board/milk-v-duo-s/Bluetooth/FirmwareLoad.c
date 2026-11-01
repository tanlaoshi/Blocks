/*
 * FirmwareLoad.c — Duo S 蓝牙固件（bt-duo-2）
 *
 * Docs/27：经 SDIO 灌 fmacfwbt（与 WiFi 同模组）；再由 UART4 跑 HCI。
 * 本文件只调 Sdhci 固件阶段钩子，不改 Wifi 数据面。
 */
#include "BluetoothPrivate.h"
#include "Sdhci.h"
#include "HalSerial.h"

int DuoBluetoothFirmwareLoad(void) {
    HalSerialBootMarkForce("Boot: Bluetooth fmacfwbt load\n");
    if (SdhciWifiWriteFirmwareBluetoothCoexistence() != 0) {
        HalSerialBootMarkForce("Boot: Bluetooth fmacfwbt load fail\n");
        return -1;
    }
    HalSerialBootMarkForce("Boot: Bluetooth fmacfwbt load ok\n");
    return 0;
}
