/*
 * SramProbe.c — pi-wifi-3：SRAM 别名同周期写读回
 *
 * 0x155：失败拆 write/read/mismatch；mismatch 的 got 编进 stage 供 SCAN 落盘。
 */
#include "Wifi/Private.h"

#define WIFI_SRAM_PROBE_BASE    0x00000000u
#define WIFI_SRAM_PROBE_PATTERN 0xA5A55A5Au

static char gWifiSramProbeFailStage[28];

static void WifiSramProbeCopyStage(const char *Text) {
    UINT32 Index;
    gWifiSramProbeFailStage[0] = 0;
    if (Text == 0) {
        return;
    }
    for (Index = 0; Text[Index] != 0 && Index + 1u < sizeof(gWifiSramProbeFailStage);
         Index++) {
        gWifiSramProbeFailStage[Index] = Text[Index];
    }
    gWifiSramProbeFailStage[Index] = 0;
}

static void WifiSramProbeSetMismatchStage(UINT32 Got) {
    static const char HexDigits[] = "0123456789abcdef";
    UINT32 Index;

    WifiSramProbeCopyStage("load-sram-");
    for (Index = 0; Index < 8u; Index++) {
        UINT32 Length = 0;
        while (gWifiSramProbeFailStage[Length] != 0) {
            Length++;
        }
        if (Length + 1u >= sizeof(gWifiSramProbeFailStage)) {
            break;
        }
        gWifiSramProbeFailStage[Length] =
            HexDigits[(Got >> ((7u - Index) * 4u)) & 0xFu];
        gWifiSramProbeFailStage[Length + 1u] = 0;
    }
}

const char *WifiSramProbeFailStage(void) {
    return gWifiSramProbeFailStage[0] ? gWifiSramProbeFailStage : "load-sram-probe";
}

int WifiBackplaneSramProbeWrite(void) {
    UINT32 ReadBack = 0;

    gWifiSramProbeFailStage[0] = 0;
    WifiBootMarkHexadecimal32("Boot: Pi WIFI sram probe base=", WIFI_SRAM_PROBE_BASE);
    HalSerialBootMarkForce(" dword\n");
    if (WifiBackplaneWrite32(WIFI_SRAM_PROBE_BASE, WIFI_SRAM_PROBE_PATTERN) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI sram probe write fail\n");
        WifiSramProbeCopyStage("load-sram-write");
        return -1;
    }
    if (WifiBackplaneRead32(WIFI_SRAM_PROBE_BASE, &ReadBack) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI sram probe read fail\n");
        WifiSramProbeCopyStage("load-sram-read");
        return -1;
    }
    if (ReadBack != WIFI_SRAM_PROBE_PATTERN) {
        WifiBootMarkHexadecimal32("Boot: Pi WIFI sram probe=no got=", ReadBack);
        HalSerialBootMarkForce("\n");
        WifiSramProbeSetMismatchStage(ReadBack);
        return -1;
    }
    HalSerialBootMarkForce("Boot: Pi WIFI sram probe=yes\n");
    return 0;
}
