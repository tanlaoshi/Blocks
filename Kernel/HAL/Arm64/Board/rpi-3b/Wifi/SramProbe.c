/*
 * SramProbe.c — 灌片前单块写探针（打 xfer 形态；失败即停）
 */
#include "Wifi/Private.h"

int WifiBackplaneSramProbeWrite(void) {
    UINT8 Probe[64];
    UINT32 Index;

    for (Index = 0; Index < 64u; Index++) {
        Probe[Index] = (UINT8)(0xA5u ^ (UINT8)Index);
    }
    if (WifiBackplaneWriteMemory(WIFI_RAM_DOWNLOAD_BASE, Probe, 64u) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI sram probe write fail\n");
        return -1;
    }
    HalSerialBootMarkForce("Boot: Pi WIFI sram probe write ok\n");
    return 0;
}
