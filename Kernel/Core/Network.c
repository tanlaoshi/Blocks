/*
 * Network.c — K11 探卡 + K20 virtio-net 最小 TX/RX
 *
 * 【初学者】
 * K11：PCI class 0x02 找到网卡。
 * K20：HalNet（legacy virtio-net）发一帧 ARP，短轮询是否收到应答。
 * 不做 lwIP / Socket / DHCP。
 */
#include "Network.h"
#include "HalNet.h"
#include "HalSerial.h"
#include "ToySerialConfig.h"

static int gNicReady;
static UINT16 gNicVid;
static UINT16 gNicDid;
static UINT8 gNicBus;
static UINT8 gNicDev;
static UINT8 gNicFunc;

int NetworkNicReady(void) {
    return gNicReady;
}

UINT16 NetworkNicVendorId(void) {
    return gNicVid;
}

UINT16 NetworkNicDeviceId(void) {
    return gNicDid;
}

#if defined(__x86_64__) || defined(_M_X64)
#define PCI_CONFIG_ADDR 0xCF8u
#define PCI_CONFIG_DATA 0xCFCu

static void Out32(UINT16 Port, UINT32 Value) {
    __asm__ volatile("outl %0, %1" : : "a"(Value), "Nd"(Port));
}

static UINT32 In32(UINT16 Port) {
    UINT32 Value;
    __asm__ volatile("inl %1, %0" : "=a"(Value) : "Nd"(Port));
    return Value;
}

static UINT32 PciConfigRead32(UINT8 Bus, UINT8 Dev, UINT8 Func, UINT8 Offset) {
    UINT32 Addr = 0x80000000u | ((UINT32)Bus << 16) | ((UINT32)(Dev & 0x1Fu) << 11) |
                  ((UINT32)(Func & 0x7u) << 8) | ((UINT32)(Offset & 0xFCu));
    Out32(PCI_CONFIG_ADDR, Addr);
    return In32(PCI_CONFIG_DATA);
}

static int ScanPciNetwork(void) {
    UINT16 Bus;
    UINT8 Dev;
    UINT8 Func;

    for (Bus = 0; Bus < 8; Bus++) {
        for (Dev = 0; Dev < 32; Dev++) {
            for (Func = 0; Func < 8; Func++) {
                UINT32 Id = PciConfigRead32((UINT8)Bus, Dev, Func, 0x00);
                UINT16 Vid = (UINT16)(Id & 0xFFFFu);
                UINT16 Did;
                UINT32 ClassReg;
                UINT8 BaseClass;

                if (Vid == 0xFFFFu || Vid == 0) {
                    if (Func == 0) {
                        break;
                    }
                    continue;
                }
                Did = (UINT16)((Id >> 16) & 0xFFFFu);
                ClassReg = PciConfigRead32((UINT8)Bus, Dev, Func, 0x08);
                BaseClass = (UINT8)((ClassReg >> 24) & 0xFFu);
                if (BaseClass != 0x02u) {
                    continue;
                }
                gNicBus = (UINT8)Bus;
                gNicDev = Dev;
                gNicFunc = Func;
                gNicVid = Vid;
                gNicDid = Did;
                gNicReady = 1;
                return 0;
            }
        }
    }
    return -1;
}

/* QEMU user 网：客户 10.0.2.15，网关 10.0.2.2；发 ARP 谁有网关 */
static void NetSendArpProbe(void) {
    UINT8 Frame[42];
    UINT8 Mac[6];
    UINTN i;
    int Rx;
    UINT8 RxBuf[1518];
    UINT32 Spin;

    HalNetGetMac(Mac);
    /* eth dst broadcast */
    for (i = 0; i < 6; i++) {
        Frame[i] = 0xFF;
    }
    for (i = 0; i < 6; i++) {
        Frame[6 + i] = Mac[i];
    }
    Frame[12] = 0x08;
    Frame[13] = 0x06; /* ARP */
    Frame[14] = 0x00;
    Frame[15] = 0x01; /* eth */
    Frame[16] = 0x08;
    Frame[17] = 0x00; /* IPv4 */
    Frame[18] = 6;
    Frame[19] = 4;
    Frame[20] = 0x00;
    Frame[21] = 0x01; /* request */
    for (i = 0; i < 6; i++) {
        Frame[22 + i] = Mac[i];
    }
    /* spa 10.0.2.15 */
    Frame[28] = 10;
    Frame[29] = 0;
    Frame[30] = 2;
    Frame[31] = 15;
    for (i = 0; i < 6; i++) {
        Frame[32 + i] = 0;
    }
    /* tpa 10.0.2.2 */
    Frame[38] = 10;
    Frame[39] = 0;
    Frame[40] = 2;
    Frame[41] = 2;

    if (HalNetTransmit(Frame, sizeof(Frame)) != 0) {
        HalSerialWriteChannel(TOY_SLOG_NET, "Net: WARN tx fail\n");
        return;
    }
    HalSerialWriteChannel(TOY_SLOG_NET, "Net: tx arp ok\n");

    for (Spin = 0; Spin < 500000u; Spin++) {
        Rx = HalNetReceive(RxBuf, sizeof(RxBuf));
        if (Rx > 0) {
            HalSerialWriteChannel(TOY_SLOG_NET, "Net: rx ok len=0x");
            HalSerialWriteChannelHex32(TOY_SLOG_NET, (UINT32)Rx);
            HalSerialWriteChannel(TOY_SLOG_NET, "\n");
            return;
        }
        __asm__ volatile("pause");
    }
    HalSerialWriteChannel(TOY_SLOG_NET, "Net: rx none (tx still ok)\n");
}
#endif

int NetworkInitialize(void) {
    gNicReady = 0;
    gNicVid = 0;
    gNicDid = 0;
    gNicBus = 0;
    gNicDev = 0;
    gNicFunc = 0;

#if defined(__x86_64__) || defined(_M_X64)
    if (ScanPciNetwork() != 0) {
        HalSerialWriteChannel(TOY_SLOG_NET, "Net: WARN no nic (PCI)\n");
        return 0;
    }
    HalSerialWriteChannel(TOY_SLOG_NET, "Net: nic ok bus=0x");
    HalSerialWriteChannelHex32(TOY_SLOG_NET, gNicBus);
    HalSerialWriteChannel(TOY_SLOG_NET, " dev=0x");
    HalSerialWriteChannelHex32(TOY_SLOG_NET, gNicDev);
    HalSerialWriteChannel(TOY_SLOG_NET, " vid=0x");
    HalSerialWriteChannelHex32(TOY_SLOG_NET, gNicVid);
    HalSerialWriteChannel(TOY_SLOG_NET, " did=0x");
    HalSerialWriteChannelHex32(TOY_SLOG_NET, gNicDid);
    HalSerialWriteChannel(TOY_SLOG_NET, "\n");

    if (HalNetInit() == 0 && HalNetReady()) {
        UINT8 Mac[6];
        HalNetGetMac(Mac);
        HalSerialWriteChannel(TOY_SLOG_NET, "Net: virtio ok mac=");
        HalSerialWriteChannelHex64(
            TOY_SLOG_NET,
            ((UINT64)Mac[0] << 40) | ((UINT64)Mac[1] << 32) | ((UINT64)Mac[2] << 24) |
                ((UINT64)Mac[3] << 16) | ((UINT64)Mac[4] << 8) | (UINT64)Mac[5]);
        HalSerialWriteChannel(TOY_SLOG_NET, "\n");
        NetSendArpProbe();
    } else {
        HalSerialWriteChannel(TOY_SLOG_NET, "Net: WARN virtio-net init fail\n");
    }
    return 0;
#else
    HalSerialWriteChannel(TOY_SLOG_NET, "Net: stub (no PCI probe)\n");
    return 0;
#endif
}
