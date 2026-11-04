/*
 * HostBootstrap.c — SD0 主机表 + 块后端（不拉 WiFi / Driver 框架）
 */
#include "Sdhci/Private.h"

static SDHCI_HOST gBootStubHosts[SDHCI_HOST_COUNT] = {
    {.Base = SDHCI_BASE_SD0, .Index = 0, .BusWidth = 1, .CardType = SDHCI_CARD_NONE},
    {.Base = SDHCI_BASE_SD1,
     .Index = 1,
     .BusWidth = 1,
     .CardType = SDHCI_CARD_NONE,
     .NonRemovable = 1},
};

SDHCI_HOST *SdhciHostAt(UINT32 Index) {
    return (Index < SDHCI_HOST_COUNT) ? &gBootStubHosts[Index] : 0;
}

static int BootStubBlockProbe(UINT32 Drive) {
    return (Drive == 0 && gBootStubHosts[0].Ready) ? 1 : 0;
}

static int BootStubBlockRead(UINT32 Drive, UINT32 LogicalBlockAddress, UINT32 Count,
                             void *Buffer) {
    UINT8 *Pointer = (UINT8 *)Buffer;
    UINT32 Index;

    (void)Drive;
    if (!gBootStubHosts[0].Ready || Buffer == 0 || Count == 0) {
        return 0;
    }
    for (Index = 0; Index < Count; Index++) {
        if (SdhciReadOneBlock(&gBootStubHosts[0], LogicalBlockAddress + Index,
                              Pointer + (UINTN)Index * 512u) != 0) {
            (void)SdhciSoftwareReset(&gBootStubHosts[0], SDHCI_RESET_CMD | SDHCI_RESET_DATA);
            return 0;
        }
    }
    return 1;
}

/*
 * ksend-rollback：须能写 boot.try（试跑计数）。
 * 约定与 Read 相同：非 0 = 成功。
 */
static int BootStubBlockWrite(UINT32 Drive, UINT32 LogicalBlockAddress, UINT32 Count,
                              const void *Buffer) {
    const UINT8 *Pointer = (const UINT8 *)Buffer;
    UINT32 Index;

    (void)Drive;
    if (!gBootStubHosts[0].Ready || Buffer == 0 || Count == 0) {
        return 0;
    }
    for (Index = 0; Index < Count; Index++) {
        if (SdhciWriteOneBlock(&gBootStubHosts[0], LogicalBlockAddress + Index,
                               Pointer + (UINTN)Index * 512u) != 0) {
            (void)SdhciSoftwareReset(&gBootStubHosts[0], SDHCI_RESET_CMD | SDHCI_RESET_DATA);
            return 0;
        }
    }
    return 1;
}

static const BLOCK_BACKEND gBootStubBlockBackend = {
    .Probe = BootStubBlockProbe,
    .ReadSectors = BootStubBlockRead,
    .WriteSectors = BootStubBlockWrite,
    .Flush = 0,
};

int BootStubSecureDigitalInitialize(void) {
    SDHCI_HOST *Host = &gBootStubHosts[0];
    int Result;

    Result = SdhciHostInitialize(Host);
    if (Result != 0) {
        return Result;
    }
    Host->Ready = 1;
    BlockRegisterBackend(&gBootStubBlockBackend);
    if (BlockInitialize() <= 0) {
        return -1;
    }
    return 0;
}
