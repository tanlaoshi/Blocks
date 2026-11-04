/*
 * WifiFirmwareBluetooth.c — bt-duo-2：fmac 阶段选 fmacfwbt + write confirm
 *
 * 只改 gAicStages[fmac] 的 Path/ExpectSize，复用现网 SdhciWifiFw 管线。
 * 禁止碰数据面 / netif / patch btmode。
 */
#include "Wifi/Private.h"
#include "Sdhci.h"

static void WifiFirmwareSelectFmacBluetoothCoexistence(void) {
    AIC_FW_STAGE *Stage = &gAicStages[AIC_FW_FMAC_STAGE_INDEX];

    Stage->Name = "fmacfwbt";
    Stage->Path = AIC_FW_FMAC_BLUETOOTH;
    Stage->ExpectSize = AIC_D80_FMAC_BLUETOOTH_SIZE;
}

int SdhciWifiWriteFirmwareBluetoothCoexistence(void) {
    WifiFirmwareSelectFmacBluetoothCoexistence();
    SdhciMark("WiFi: fmac stage -> fmacfwbt_8800d80_u02.bin (bt-duo-2)\n");
    return SdhciWifiFw(SDHCI_WIFI_FW_WRITE | SDHCI_WIFI_FW_CONFIRM);
}
