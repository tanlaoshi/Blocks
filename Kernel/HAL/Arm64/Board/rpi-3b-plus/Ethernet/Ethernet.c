/*
 * Ethernet.c — Pi3B+ LAN9514 门面（bd-pi-eth；对表 U-Boot smsc95xx）
 * L2：vendor REG + bulk；Poll 最多一次短 RX（鼠优先）。
 */
#include "Ethernet/Private.h"

int gSmscReady;
int gSmscBound;
int gSmscLinkWasUp;
int gSmscLwIpRx;
UINT8 gSmscAddr;
UINT8 gSmscHub;
UINT8 gSmscPort;
UINT8 gSmscEpIn;
UINT8 gSmscEpOut;
UINT16 gSmscBulkMps;
UINT8 gSmscTxToggle;
UINT8 gSmscRxToggle;
UINT32 gSmscMacCr;
UINT8 gSmscMac[6];
UINT32 gSmscIp;
UINT32 gSmscTxDone;
UINT32 gSmscRxFrames;
UINT8 gSmscTxBuf[8u + 1518u] __attribute__((aligned(64)));
UINT8 gSmscRxBuf[RX_URB_SIZE] __attribute__((aligned(64)));

int BoardEthernetUsbAttach(UINT8 DevAddr, UINT8 Hub, UINT8 Port,
                           UINT8 EpBulkIn, UINT8 EpBulkOut, UINT16 BulkMps) {
    if (gSmscReady) {
        return 0;
    }
    gSmscAddr = DevAddr;
    gSmscHub = Hub;
    gSmscPort = Port;
    gSmscEpIn = EpBulkIn & 0x7Fu;
    gSmscEpOut = EpBulkOut & 0x7Fu;
    gSmscBulkMps = BulkMps ? BulkMps : 512u;
    gSmscTxToggle = 0;
    gSmscRxToggle = 0;
    gSmscIp = 0;
    if (SmscInitMac() != 0) {
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

static int EthReady(void) {
    return gSmscReady && gSmscBound;
}

static void EthPoll(void) {
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
    SmscSelectDev();
    Got = BoardDwc2Bulk(gSmscAddr, gSmscEpIn, 1, gSmscRxBuf, (int)RX_URB_SIZE, gSmscBulkMps,
                        &gSmscRxToggle);
    if (Got > 4) {
        SmscHandleRx(gSmscRxBuf, Got);
    }
}

static void EthGetMac(UINT8 Mac[6]) {
    int i;
    if (!Mac) {
        return;
    }
    for (i = 0; i < 6; i++) {
        Mac[i] = gSmscMac[i];
    }
}

static UINT32 EthGetIp(void) {
    return gSmscIp;
}

static void EthSetIp(UINT32 Ip) {
    gSmscIp = Ip;
}

static void EthFormatIp(UINT32 Ip, char *Buf, int BufLen) {
    if (!Buf || BufLen < 16) {
        return;
    }
    FormatDottedDecimal(Ip, Buf, BufLen);
}

static int EthParseIp(const char *Text, UINT32 *Ip) {
    UINT32 Parts[4];
    int Part = 0;
    UINT32 Val = 0;
    int Digits = 0;

    if (!Text || !Ip) {
        return -1;
    }
    while (*Text) {
        if (*Text >= '0' && *Text <= '9') {
            Val = Val * 10u + (UINT32)(*Text - '0');
            if (Val > 255) {
                return -1;
            }
            Digits++;
        } else if (*Text == '.') {
            if (Digits == 0 || Part >= 3) {
                return -1;
            }
            Parts[Part++] = Val;
            Val = 0;
            Digits = 0;
        } else {
            return -1;
        }
        Text++;
    }
    if (Digits == 0 || Part != 3) {
        return -1;
    }
    Parts[3] = Val;
    *Ip = (Parts[0] << 24) | (Parts[1] << 16) | (Parts[2] << 8) | Parts[3];
    return 0;
}

static UINT16 EthChecksum(const void *Data, UINTN Len) {
    const UINT8 *B = (const UINT8 *)Data;
    UINT32 Sum = 0;
    UINTN i;

    for (i = 0; i + 1 < Len; i += 2) {
        Sum += ((UINT32)B[i] << 8) | B[i + 1];
    }
    if (i < Len) {
        Sum += (UINT32)B[i] << 8;
    }
    while (Sum >> 16) {
        Sum = (Sum & 0xFFFFu) + (Sum >> 16);
    }
    return (UINT16)~Sum;
}

static int EthPing(const char *Host, int TimeoutMs) {
    (void)Host;
    (void)TimeoutMs;
    return -1;
}

static void EthGetStats(UINT32 *Tx, UINT32 *Rx) {
    if (Tx) {
        *Tx = gSmscTxDone;
    }
    if (Rx) {
        *Rx = gSmscRxFrames;
    }
}

static int EthSendIp(UINT32 Dst, UINT8 Proto, const void *Payload, UINTN Len) {
    (void)Dst;
    (void)Proto;
    (void)Payload;
    (void)Len;
    return -1;
}

static void EthSetLwIpRx(int Enable) {
    gSmscLwIpRx = Enable ? 1 : 0;
}

static const NET_BACKEND gBackend = {
    .Ready = EthReady,
    .Poll = EthPoll,
    .GetMac = EthGetMac,
    .GetIp = EthGetIp,
    .SetIp = EthSetIp,
    .FormatIp = EthFormatIp,
    .ParseIp = EthParseIp,
    .Ping = EthPing,
    .GetStats = EthGetStats,
    .SendIp = EthSendIp,
    .Checksum = EthChecksum,
    .SetLwIpRx = EthSetLwIpRx,
};

static int EthDriverProbe(const DRIVER *Self, void *Bus, void **Out) {
    (void)Self;
    (void)Bus;
    if (!gSmscReady) {
        return -1;
    }
    if (Out) {
        *Out = 0;
    }
    return 0;
}

static int EthDriverBind(DRIVER_INSTANCE *Inst) {
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

static void EthDriverRemove(DRIVER_INSTANCE *Inst) {
    (void)Inst;
    gSmscBound = 0;
}

static const DRIVER gEthDriver = {
    .Name = "smsc95xx",
    .Class = DRIVER_CLASS_NET,
    .Match = 0,
    .Probe = EthDriverProbe,
    .Bind = EthDriverBind,
    .Remove = EthDriverRemove,
};

void BoardEthernetRegister(void) {
    (void)DriverRegister(&gEthDriver);
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
