/*
 * SramProbe.c — pi-wifi-3：SRAM 别名同周期写读回
 *
 * 0x135：上游先 CHIPCLKCSR 0x00→0x08；本文件仍 4B 探 @0。
 */
#include "Wifi/Private.h"

#define WIFI_SRAM_PROBE_BASE 0x00000000u
#define WIFI_SRAM_PROBE_PATTERN 0xA5A55A5Au

int WifiBackplaneSramProbeWrite(void) {
    UINT32 ReadBack = 0;

    WifiBootMarkHexadecimal32("Boot: Pi WIFI sram probe base=", WIFI_SRAM_PROBE_BASE);
    HalSerialBootMarkForce(" dword\n");
    if (WifiBackplaneWrite32(WIFI_SRAM_PROBE_BASE, WIFI_SRAM_PROBE_PATTERN) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI sram probe write fail\n");
        return -1;
    }
    if (WifiBackplaneRead32(WIFI_SRAM_PROBE_BASE, &ReadBack) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI sram probe read fail\n");
        return -1;
    }
    if (ReadBack != WIFI_SRAM_PROBE_PATTERN) {
        WifiBootMarkHexadecimal32("Boot: Pi WIFI sram probe=no got=", ReadBack);
        HalSerialBootMarkForce("\n");
        return -1;
    }
    HalSerialBootMarkForce("Boot: Pi WIFI sram probe=yes\n");
    return 0;
}
