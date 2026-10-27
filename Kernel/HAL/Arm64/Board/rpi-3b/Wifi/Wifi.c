/*
 * Wifi.c — Pi3B WiFi 门面：探针 + HalWifi ops（pwifi-1）
 */
#include "Wifi/Private.h"
#include "HalDevices.h"

static int WifiOpsClaim(void) {
    if (!gWifiChipProbed) {
        return 0;
    }
    /* FS 就绪后 Claim 才看得见盘上固件；启动瞬间 miss 不挡有芯 */
    WifiFirmwarePathProbe();
    return 1;
}

static int WifiOpsReady(void) {
    /* 本刀：有芯即 Ready；无芯绝不谎称。固件灌片 / 关联 → pwifi-2 */
    return gWifiChipProbed ? 1 : 0;
}

int BoardWifiClaim(void) {
    return WifiOpsClaim();
}

int BoardWifiReady(void) {
    return WifiOpsReady();
}

static int WifiProbeOnce(void) {
    WifiPinConfigure();
    if (WifiWirelessPowerOn() != 0) {
        return -1;
    }
    if (WifiSdioHostBringUp() != 0) {
        return -1;
    }
    if (WifiSdioEnumerate() != 0) {
        return -1;
    }
    if (WifiSdioEnableFunction1() != 0) {
        return -1;
    }
    if (WifiChipIdentityProbe() != 0) {
        return -1;
    }
    {
        const char *Path = WifiFirmwarePathForChip();
        HalSerialBootMark("Boot: Pi WIFI fw path=");
        HalSerialBootMark(Path ? Path : "(none)");
        HalSerialBootMark(" (Claim checks disk)\n");
    }
    return 0;
}

void BoardWifiRegister(void) {
    static const HAL_WIFI_OPS Ops = {
        .Claim = WifiOpsClaim,
        .Ready = WifiOpsReady,
    };

    HalWifiSetOps(&Ops);
    if (WifiProbeOnce() != 0) {
        HalSerialBootMark("Boot: Pi WIFI probe fail (Ready=0)\n");
        gWifiChipProbed = 0;
        return;
    }
    HalSerialBootMark("Boot: Pi WIFI ok\n");
}
