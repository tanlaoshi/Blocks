/*
 * Sdhci.c — Pi3B Arasan SDHCI 门面（WiFi SDIO 宿主，非 µSD 块设备）
 *
 * µSD 块读写 = BoardSdhostRegister。本文件不再提供 BLOCK_BACKEND：
 * 旧 gSdhciBackend / SdhciWrite(扇区) 是「只读刀」死桩且从未再挂块，已删。
 * Arasan 寄存器与 HostPrepare 仍供 Wifi/ 下 SDIO 使用。
 */
#include "Sdhci/Private.h"

UINT16 gSdhciHostSpecification;
UINT32 gSdhciLastCommandStatus;
UINT32 gSdhciLastPresentState;

void BoardSdhciRegister(void) {
    /* 刻意空：勿 DriverBlockAttach；WiFi 自行 SdhciHostPrepare */
}
