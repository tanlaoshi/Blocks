/*
 * Network.c — K11：探网卡最小子集
 *
 * 【初学者】
 * 完整路径是驱动绑定 → 收发包 → lwIP → Socket。
 * 本刀只做 PCI 配置空间扫描：找到 Base Class = 0x02（Network）即过线。
 * 不碰 BAR MMIO、不开中断、不收包。
 */
#include "Network.h"
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
    UINT32 Addr;

    Addr = 0x80000000u |
           ((UINT32)Bus << 16) |
           ((UINT32)(Dev & 0x1Fu) << 11) |
           ((UINT32)(Func & 0x7u) << 8) |
           ((UINT32)(Offset & 0xFCu));
    Out32(PCI_CONFIG_ADDR, Addr);
    return In32(PCI_CONFIG_DATA);
}

/* 扫有限总线：QEMU q35 上 virtio-net 通常在 bus0 */
static int ScanPciNetwork(void) {
    UINT16 Bus;
    UINT8 Dev;
    UINT8 Func;

    for (Bus = 0; Bus < 8; Bus++) {
        for (Dev = 0; Dev < 32; Dev++) {
            for (Func = 0; Func < 8; Func++) {
                UINT32 Id;
                UINT32 ClassReg;
                UINT8 BaseClass;
                UINT16 Vid;
                UINT16 Did;

                Id = PciConfigRead32((UINT8)Bus, Dev, Func, 0x00);
                Vid = (UINT16)(Id & 0xFFFFu);
                if (Vid == 0xFFFFu || Vid == 0) {
                    if (Func == 0) {
                        break; /* 无多功能 */
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
    return 0;
#else
    HalSerialWriteChannel(TOY_SLOG_NET, "Net: stub (no PCI probe)\n");
    return 0;
#endif
}
