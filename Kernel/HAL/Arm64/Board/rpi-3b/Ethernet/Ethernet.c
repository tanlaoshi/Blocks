/*
 * Ethernet.c — Pi3B LAN9514 门面（bd-pi-eth；对表 U-Boot smsc95xx）
 * L2：vendor REG + bulk；Poll 最多一次短 RX（鼠优先）。
 */
#include "Ethernet/Private.h"

int gSmscReady;
int gSmscBound;
int gSmscLinkWasUp;
int gSmscLwIpReceive;
UINT8 gSmscAddress;
UINT8 gSmscHub;
UINT8 gSmscPort;
UINT8 gSmscEndpointIn;
UINT8 gSmscEndpointOut;
UINT16 gSmscBulkMaxPacketSize;
UINT8 gSmscTransmitToggle;
UINT8 gSmscReceiveToggle;
UINT32 gSmscMacControl;
UINT8 gSmscMac[6];
UINT32 gSmscIp;
UINT32 gSmscTransmitDone;
UINT32 gSmscReceiveFrames;
UINT8 gSmscTransmitBuffer[8u + 1518u] __attribute__((aligned(64)));
UINT8 gSmscReceiveBuffer[RX_URB_SIZE] __attribute__((aligned(64)));

int BoardEthernetUsbAttach(UINT8 DevAddr, UINT8 Hub, UINT8 Port,
                           UINT8 EpBulkIn, UINT8 EpBulkOut, UINT16 BulkMps) {
    if (gSmscReady) {
        return 0;
    }
    gSmscAddress = DevAddr;
    gSmscHub = Hub;
    gSmscPort = Port;
    gSmscEndpointIn = EpBulkIn & 0x7Fu;
    gSmscEndpointOut = EpBulkOut & 0x7Fu;
    gSmscBulkMaxPacketSize = BulkMps ? BulkMps : 512u;
    gSmscTransmitToggle = 0;
    gSmscReceiveToggle = 0;
    gSmscIp = 0;
    if (SmscMacInitialize() != 0) {
        HalSerialBootMark("Boot: SMSC init fail\n");
        return -1;
    }
    gSmscReady = 1;
    HalSerialBootMark("Boot: SMSC eth ok mac=");
    {
        static const char Hex[] = "0123456789ABCDEF";
        char B[3];
        int i;
        for (i = 0; i < 6; i++) {
            B[0] = Hex[(gSmscMac[i] >> 4) & 0xF];
            B[1] = Hex[gSmscMac[i] & 0xF];
            B[2] = 0;
            HalSerialBootMark(B);
            if (i < 5) {
                HalSerialBootMark(":");
            }
        }
        HalSerialBootMark("\n");
    }
    return 0;
}

static int Ready(void) {
    return gSmscReady && gSmscBound;
}

static void Poll(void) {
    int Got;
    int Up = 0;
    static UINT32 sLinkDiv;

    if (!gSmscReady) {
        return;
    }
    /*
     * 链路 down→up：再 bump epoch（对表 X64 换主槽），Shell/Worker 自动 DHCP。
     * 鼠优先：MII 非每拍（未 up 时每拍找链路；已 up 每 64 拍复核）。
     */
    sLinkDiv++;
    if (!gSmscLinkWasUp || (sLinkDiv & 63u) == 0u) {
        static int sDownStreak;

        if (BoardEthernetGetLink(&Up, 0, 0) && Up) {
            sDownStreak = 0;
            if (!gSmscLinkWasUp) {
                gSmscLinkWasUp = 1;
                HalNetNicEpochBump();
            }
        } else if (gSmscLinkWasUp) {
            if (++sDownStreak >= 3) {
                gSmscLinkWasUp = 0;
                sDownStreak = 0;
            }
        }
    }
    SmscSelectDevice();
    Got = BoardUsbHostControllerBulk(gSmscAddress, gSmscEndpointIn, 1, gSmscReceiveBuffer, (int)RX_URB_SIZE, gSmscBulkMaxPacketSize,
                        &gSmscReceiveToggle);
    if (Got > 4) {
        SmscHandleReceive(gSmscReceiveBuffer, Got);
    }
}

static void GetMac(UINT8 Mac[6]) {
    int i;
    if (!Mac) {
        return;
    }
    for (i = 0; i < 6; i++) {
        Mac[i] = gSmscMac[i];
    }
}

static UINT32 GetIp(void) {
    return gSmscIp;
}

static void SetIp(UINT32 Ip) {
    gSmscIp = Ip;
}

static void FormatIp(UINT32 Ip, char *Buffer, int BufLen) {
    if (!Buffer || BufLen < 16) {
        return;
    }
    FormatDottedDecimal(Ip, Buffer, BufLen);
}

static int ParseIp(const char *Text, UINT32 *Ip) {
    UINT32 Parts[4];
    int Part = 0;
    UINT32 Value = 0;
    int Digits = 0;

    if (!Text || !Ip) {
        return -1;
    }
    while (*Text) {
        if (*Text >= '0' && *Text <= '9') {
            Value = Value * 10u + (UINT32)(*Text - '0');
            if (Value > 255) {
                return -1;
            }
            Digits++;
        } else if (*Text == '.') {
            if (Digits == 0 || Part >= 3) {
                return -1;
            }
            Parts[Part++] = Value;
            Value = 0;
            Digits = 0;
        } else {
            return -1;
        }
        Text++;
    }
    if (Digits == 0 || Part != 3) {
        return -1;
    }
    Parts[3] = Value;
    *Ip = (Parts[0] << 24) | (Parts[1] << 16) | (Parts[2] << 8) | Parts[3];
    return 0;
}

static UINT16 Checksum(const void *Data, UINTN Length) {
    const UINT8 *B = (const UINT8 *)Data;
    UINT32 Sum = 0;
    UINTN i;

    for (i = 0; i + 1 < Length; i += 2) {
        Sum += ((UINT32)B[i] << 8) | B[i + 1];
    }
    if (i < Length) {
        Sum += (UINT32)B[i] << 8;
    }
    while (Sum >> 16) {
        Sum = (Sum & 0xFFFFu) + (Sum >> 16);
    }
    return (UINT16)~Sum;
}

static int Ping(const char *Host, int TimeoutMs) {
    (void)Host;
    (void)TimeoutMs;
    return -1;
}

static void GetStats(UINT32 *TransmitDone, UINT32 *ReceiveFrames) {
    if (TransmitDone) {
        *TransmitDone = gSmscTransmitDone;
    }
    if (ReceiveFrames) {
        *ReceiveFrames = gSmscReceiveFrames;
    }
}

static int SendIp(UINT32 Destination, UINT8 Proto, const void *Payload, UINTN Length) {
    (void)Destination;
    (void)Proto;
    (void)Payload;
    (void)Length;
    return -1;
}

static void SetLwIpReceive(int Enable) {
    gSmscLwIpReceive = Enable ? 1 : 0;
}

static const NET_BACKEND gBackend = {
    .Ready = Ready,
    .Poll = Poll,
    .GetMac = GetMac,
    .GetIp = GetIp,
    .SetIp = SetIp,
    .FormatIp = FormatIp,
    .ParseIp = ParseIp,
    .Ping = Ping,
    .GetStats = GetStats,
    .SendIp = SendIp,
    .Checksum = Checksum,
    .SetLwIpRx = SetLwIpReceive,
};

static int DriverProbe(const DRIVER *Self, void *Bus, void **OutValue) {
    (void)Self;
    (void)Bus;
    if (!gSmscReady) {
        return -1;
    }
    if (OutValue) {
        *OutValue = 0;
    }
    return 0;
}

static int DriverBind(DRIVER_INSTANCE *Inst) {
    (void)Inst;
    if (!gSmscReady) {
        return -1;
    }
    if (DriverNetAttach(&gBackend) != 0) {
        return -1;
    }
    gSmscBound = 1;
    HalNetNicEpochBump();
    HalSerialBootMark("Boot: SMSC ETH bind\n");
    return 0;
}

static void DriverRemove(DRIVER_INSTANCE *Inst) {
    (void)Inst;
    gSmscBound = 0;
}

static const DRIVER gDriver = {
    .Name = "smsc95xx",
    .Class = DRIVER_CLASS_NET,
    .Match = 0,
    .Probe = DriverProbe,
    .Bind = DriverBind,
    .Remove = DriverRemove,
};

void BoardEthernetRegister(void) {
    (void)DriverRegister(&gDriver);
}

int BoardEthernetProbe(void) {
    return gSmscReady ? 0 : -1;
}

int BoardEthernetGetLink(int *Up, UINT32 *Mbps, int *FullDuplex) {
    UINT16 Bmsr = 0;

    if (!gSmscReady) {
        return 0;
    }
    /*
     * IEEE 802.3 BMSR.Link Status 是锁存低：掉线后保持 0 直到读一次。
     * 只读一次会把「曾掉线」当成当前 down。标准：读两次，以第二次为准。
     */
    if (SmscMiiRead(MII_BMSR, &Bmsr) != 0) {
        return 0;
    }
    if (SmscMiiRead(MII_BMSR, &Bmsr) != 0) {
        return 0;
    }
    if (Up) {
        *Up = (Bmsr & BMSR_LSTATUS) ? 1 : 0;
    }
    if (Mbps) {
        *Mbps = 100u;
    }
    if (FullDuplex) {
        *FullDuplex = 1;
    }
    return 1;
}
