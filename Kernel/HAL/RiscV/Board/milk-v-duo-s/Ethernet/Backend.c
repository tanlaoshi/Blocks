#include "Ethernet/Private.h"

const NET_BACKEND gEthernetBackend = {
    .Ready = DuoEthernetReady,
    .Poll = DuoEthernetPoll,
    .GetMac = DuoEthernetGetMac,
    .GetIp = DuoEthernetGetIp,
    .SetIp = DuoEthernetSetIp,
    .FormatIp = DuoEthernetFormatIp,
    .ParseIp = DuoEthernetParseIp,
    .Ping = DuoEthernetPing,
    .GetStats = DuoEthernetGetStats,
    .SendIp = DuoEthernetSendIp,
    .Checksum = DuoEthernetChecksum,
    .SetLwIpRx = DuoEthernetSetLwIpReceive,
};

int EthernetDriverProbe(const DRIVER *Self, void *BusContext,
                               void **OutPrivate) {
    (void)Self;
    (void)BusContext;
    if (gDuoDmaOk) {
        if (OutPrivate) {
            *OutPrivate = 0;
        }
        return 0;
    }
    if (BoardEthernetProbe() != 0 || !gDuoDmaOk) {
        return -1;
    }
    if (OutPrivate) {
        *OutPrivate = 0;
    }
    return 0;
}

int EthernetDriverBind(DRIVER_INSTANCE *Instance) {
    (void)Instance;
    if (!gDuoDmaOk) {
        return -1;
    }
    if (DriverNetAttach(&gEthernetBackend) != 0) {
        return -1;
    }
    gDuoBound = 1;
    HalNetNicEpochBump();
    HalSerialBootMark("Boot: Duo ETH bind\n");
    return 0;
}

void EthernetDriverRemove(DRIVER_INSTANCE *Instance) {
    (void)Instance;
    gDuoBound = 0;
}

const DRIVER gEthernetDriver = {
    .Name = "cv181x-ethernet",
    .Class = DRIVER_CLASS_NET,
    .Match = 0,
    .Probe = EthernetDriverProbe,
    .Bind = EthernetDriverBind,
    .Remove = EthernetDriverRemove,
};
