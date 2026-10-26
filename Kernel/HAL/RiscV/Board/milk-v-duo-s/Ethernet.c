/*
 * Ethernet.c — Duo S 以太网 MAC（net-1…4）
 *
 * MDIO：dwmac1000 MII Address/Data；phy<<11 reg<<6（dwmac1000_core.c）。
 * PHY：优先地址 1；Basic Status 读两次消锁存。
 * DMA：normal 描述符（ENHDESSEL=0）；先 LM 自检再关 LM 常驻收发。
 * net-4：DRIVER_CLASS_NET → DriverNetAttach；BOARD_HAS_NET；非 VirtioNet。
 */
#include "Ethernet.h"
#include "Board.h"
#include "Compiler.h"
#include "Driver.h"
#include "DriverNet.h"
#include "Hal.h"
#include "HalDevices.h"
#include "SerialLog.h"
#include "Text.h"
#ifdef LWIP_ENABLE
#include "Netif.h"
#endif

typedef struct {
    UINT32 Descriptor0;
    UINT32 Descriptor1;
    UINT32 Descriptor2;
    UINT32 Descriptor3;
} EthernetDmaDescriptor;

static EthernetDmaDescriptor gTransmitDescriptor[DUO_ETHERNET_DMA_RING_COUNT]
    __attribute__((aligned(64)));
static EthernetDmaDescriptor gReceiveDescriptor[DUO_ETHERNET_DMA_RING_COUNT]
    __attribute__((aligned(64)));
static UINT8 gTransmitBuffer[DUO_ETHERNET_DMA_RING_COUNT][DUO_ETHERNET_DMA_BUFFER_SIZE]
    __attribute__((aligned(64)));
static UINT8 gReceiveBuffer[DUO_ETHERNET_DMA_RING_COUNT][DUO_ETHERNET_DMA_BUFFER_SIZE]
    __attribute__((aligned(64)));

static int gMacOk;
static int gPhyOk;
static int gDmaOk;
static int gBound;
static UINT32 gPhyAddress;
static UINT8 gMac[6];
static UINT32 gIp = HAL_NET_IP_DEFAULT;
static UINT32 gTransmitIndex;
static UINT32 gReceiveIndex;
static UINT32 gTransmitDone;
static UINT32 gReceiveFrames;
static int gLwIpReceive;

/* net-5：ARP / ICMP（对齐 Virtio Net Protocol·Arp） */
#define ETHERNET_ETH_HEADER_LENGTH       14u
#define ETHERNET_IP_HEADER_LENGTH        20u
#define ETHERNET_ICMP_HEADER_LENGTH      8u
#define ETHERNET_ETH_TYPE_ARP            0x0806u
#define ETHERNET_ETH_TYPE_IP             0x0800u
#define ETHERNET_ARP_OPERATION_REQUEST   1u
#define ETHERNET_ARP_OPERATION_REPLY     2u
#define ETHERNET_IP_PROTOCOL_ICMP        1u
#define ETHERNET_ICMP_ECHO_REQUEST       8u
#define ETHERNET_ICMP_ECHO_REPLY         0u
#define ETHERNET_ARP_CACHE_SIZE          4u
#define ETHERNET_PING_PAYLOAD_LENGTH     32u

typedef struct {
    UINT32 Ip;
    UINT8 Mac[6];
    int Valid;
} EthernetArpEntry;

static EthernetArpEntry gArpCache[ETHERNET_ARP_CACHE_SIZE];
static int gPingWait;
static UINT32 gPingTarget;
static UINT16 gPingId = 0x4F53u;
static UINT16 gPingSequence;

static void MemoryCopy(void *Destination, const void *Source, UINTN Length) {
    UINT8 *Out = (UINT8 *)Destination;
    const UINT8 *In = (const UINT8 *)Source;
    UINTN Index;

    for (Index = 0; Index < Length; Index++) {
        Out[Index] = In[Index];
    }
}

static void MemorySet(void *Destination, UINT8 Value, UINTN Length) {
    UINT8 *Out = (UINT8 *)Destination;
    UINTN Index;

    for (Index = 0; Index < Length; Index++) {
        Out[Index] = Value;
    }
}

static UINT16 ByteSwap16(UINT16 Value) {
    return (UINT16)((Value << 8) | (Value >> 8));
}

static UINT32 ByteSwap32(UINT32 Value) {
    return ((Value & 0x000000FFu) << 24) | ((Value & 0x0000FF00u) << 8) |
           ((Value & 0x00FF0000u) >> 8) | ((Value & 0xFF000000u) >> 24);
}

static UINT16 EthernetChecksum(const void *Data, UINTN Length);
int BoardEthernetSendFrame(const UINT8 *Frame, UINTN FrameLength);
static void EthernetPoll(void);
static int EthernetParseIp(const char *Text, UINT32 *Ip);
static int EthernetReady(void);
static int DmaStartWire(void);

typedef struct {
    UINT8 Destination[6];
    UINT8 Source[6];
    UINT16 EtherType;
} BLOCKS_PACKED EthernetEthHeader;

typedef struct {
    UINT16 HardwareType;
    UINT16 ProtocolType;
    UINT8 HardwareLength;
    UINT8 ProtocolLength;
    UINT16 Operation;
    UINT8 SenderMac[6];
    UINT32 SenderIp;
    UINT8 TargetMac[6];
    UINT32 TargetIp;
} BLOCKS_PACKED EthernetArpPacket;

typedef struct {
    UINT8 VersionIhl;
    UINT8 TypeOfService;
    UINT16 TotalLength;
    UINT16 Identification;
    UINT16 Fragment;
    UINT8 TimeToLive;
    UINT8 Protocol;
    UINT16 Checksum;
    UINT32 Source;
    UINT32 Destination;
} BLOCKS_PACKED EthernetIpHeader;

typedef struct {
    UINT8 Type;
    UINT8 Code;
    UINT16 Checksum;
    UINT16 Id;
    UINT16 Sequence;
} BLOCKS_PACKED EthernetIcmpHeader;

static volatile UINT32 *Register(UINT64 Address) {
    return (volatile UINT32 *)(UINTN)Address;
}

static void Spin(UINT32 Count) {
    volatile UINT32 Index;
    for (Index = 0; Index < Count; Index++) {
        __asm__ volatile("nop");
    }
}

static void DelayMilliseconds(UINT32 Milliseconds) {
    UINT32 Index;
    for (Index = 0; Index < Milliseconds; Index++) {
        Spin(80000u);
    }
}

static void MarkHex(const char *Tag, UINT32 Value) {
    char Buffer[9];
    HalSerialBootMark(Tag);
    HalSerialFormatHex(Buffer, Value, 8);
    HalSerialBootMark(Buffer);
}

static void MarkHex16(UINT32 Value) {
    char Buffer[5];
    HalSerialFormatHex(Buffer, Value & 0xFFFFu, 4);
    HalSerialBootMark(Buffer);
}

static int VersionIsLive(UINT32 Version) {
    return Version != 0u && Version != 0xFFFFFFFFu;
}

static int MiiWaitIdle(void) {
    UINT32 Index;
    for (Index = 0; Index < 100000u; Index++) {
        if ((*Register(DUO_ETHERNET_BASE + DUO_ETHERNET_MII_ADDRESS) & DUO_ETHERNET_MII_BUSY) ==
            0u) {
            return 0;
        }
        Spin(50u);
    }
    return -1;
}

static int MiiRead(UINT32 PhyAddress, UINT32 RegisterIndex, UINT16 *OutValue) {
    UINT32 Command;

    if (MiiWaitIdle() != 0) {
        return -1;
    }
    Command = DUO_ETHERNET_MII_BUSY |
              ((PhyAddress & 0x1Fu) << DUO_ETHERNET_MII_PHY_SHIFT) |
              ((RegisterIndex & 0x1Fu) << DUO_ETHERNET_MII_REGISTER_SHIFT) |
              (DUO_ETHERNET_MII_CLOCK_RANGE << DUO_ETHERNET_MII_CLOCK_RANGE_SHIFT);
    *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_MII_ADDRESS) = Command;
    if (MiiWaitIdle() != 0) {
        return -1;
    }
    *OutValue =
        (UINT16)(*Register(DUO_ETHERNET_BASE + DUO_ETHERNET_MII_DATA) & 0xFFFFu);
    return 0;
}

static int MiiWrite(UINT32 PhyAddress, UINT32 RegisterIndex, UINT16 Value) {
    UINT32 Command;

    if (MiiWaitIdle() != 0) {
        return -1;
    }
    *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_MII_DATA) = (UINT32)Value;
    Command = DUO_ETHERNET_MII_BUSY | DUO_ETHERNET_MII_WRITE |
              ((PhyAddress & 0x1Fu) << DUO_ETHERNET_MII_PHY_SHIFT) |
              ((RegisterIndex & 0x1Fu) << DUO_ETHERNET_MII_REGISTER_SHIFT) |
              (DUO_ETHERNET_MII_CLOCK_RANGE << DUO_ETHERNET_MII_CLOCK_RANGE_SHIFT);
    *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_MII_ADDRESS) = Command;
    return MiiWaitIdle();
}

/*
 * U-Boot cv181x_ephy_id_init：放复位 / ANA 上电 / 写 PHY ID。
 * 末尾保持 APB，交给 Configure（Linux cvitek config_init）。
 */
static void PhyInitialize(void) {
    *Register(DUO_EPHY_APB_SELECT) = 1u;

    *Register(DUO_EPHY_CONTROL) = 0x0900u;
    *Register(DUO_EPHY_CONTROL) = 0x0904u;

    *Register(DUO_EPHY_PAGE_SELECT) = 0x0500u;
    *Register(DUO_EPHY_PAGE5_POWER) = 0x0c00u;
    *Register(DUO_EPHY_PAGE5_POWER) = 0x0c7eu;

    *Register(DUO_EPHY_CONTROL) = 0x0906u;

    *Register(DUO_EPHY_PAGE_SELECT) = 0x0500u;
    *Register(DUO_EPHY_APB_IDENTIFIER_1) = DUO_EPHY_IDENTIFIER_1_VALUE;
    *Register(DUO_EPHY_APB_IDENTIFIER_2) = DUO_EPHY_IDENTIFIER_2_VALUE;
}

/*
 * Linux drivers/net/phy/cvitek.c · cv182xa_phy_config_init（无 efuse 走 else 默认；
 * CONFIG_ARCH_CV181X LPF/HPF）。收尾 0x090e 启自协商 + BMCR 全双工，再交 MDIO。
 */
static void PhyConfigure(void) {
    *Register(DUO_EPHY_APB_SELECT) = 1u;
    *Register(DUO_EPHY_PAGE_SELECT) = 0x0000u;

    *Register(DUO_EPHY_APB(0x19u)) = 0x5a5au;
    *Register(DUO_EPHY_APB(0x15u)) = 0x0000u;
    *Register(DUO_EPHY_APB(0x16u)) = 0x0bb0u;

    *Register(DUO_EPHY_APB(0x17u)) = 0x0c10u;
    *Register(DUO_EPHY_APB(0x1Au)) = 0x0003u;
    *Register(DUO_EPHY_APB(0x15u)) = 0x0000u;

    *Register(DUO_EPHY_PAGE_SELECT) = 0x1000u;
    *Register(DUO_EPHY_APB(0x1Au)) = 0x1000u;
    *Register(DUO_EPHY_APB(0x1Bu)) = 0x3020u;
    *Register(DUO_EPHY_APB(0x1Cu)) = 0x5040u;
    *Register(DUO_EPHY_APB(0x1Du)) = 0x7060u;
    *Register(DUO_EPHY_APB(0x16u)) = 0x1708u;
    *Register(DUO_EPHY_APB(0x17u)) = 0x3827u;
    *Register(DUO_EPHY_APB(0x18u)) = 0x5748u;
    *Register(DUO_EPHY_APB(0x19u)) = 0x7867u;

    *Register(DUO_EPHY_PAGE_SELECT) = 0x1100u;
    *Register(DUO_EPHY_APB(0x10u)) = 0x9080u;
    *Register(DUO_EPHY_APB(0x11u)) = 0xb0a0u;
    *Register(DUO_EPHY_APB(0x12u)) = 0xd0c0u;
    *Register(DUO_EPHY_APB(0x13u)) = 0xf0e0u;
    *Register(DUO_EPHY_APB(0x14u)) = 0x9788u;
    *Register(DUO_EPHY_APB(0x15u)) = 0xb8a7u;
    *Register(DUO_EPHY_APB(0x16u)) = 0xd7c8u;
    *Register(DUO_EPHY_APB(0x17u)) = 0xf8e7u;

    *Register(DUO_EPHY_PAGE_SELECT) = 0x0500u;
    *Register(DUO_EPHY_PAGE5_POWER) =
        *Register(DUO_EPHY_PAGE5_POWER) | 0x0001u;
    *Register(DUO_EPHY_APB(0x13u)) =
        *Register(DUO_EPHY_APB(0x13u)) | 0x0820u;

    *Register(DUO_EPHY_PAGE_SELECT) = 0x0a00u;
    *Register(DUO_EPHY_APB(0x10u)) = 0x3e00u;
    *Register(DUO_EPHY_APB(0x11u)) = 0x7864u;
    *Register(DUO_EPHY_APB(0x12u)) = 0x6470u;
    *Register(DUO_EPHY_APB(0x13u)) = 0x5f62u;
    *Register(DUO_EPHY_APB(0x14u)) = 0x5a5au;
    *Register(DUO_EPHY_APB(0x15u)) = 0x5458u;
    *Register(DUO_EPHY_APB(0x16u)) = 0xb23au;
    *Register(DUO_EPHY_APB(0x17u)) = 0x94a0u;
    *Register(DUO_EPHY_APB(0x18u)) = 0x9092u;
    *Register(DUO_EPHY_APB(0x19u)) = 0x8a8eu;
    *Register(DUO_EPHY_APB(0x1Au)) = 0x8688u;
    *Register(DUO_EPHY_APB(0x1Bu)) = 0x8484u;
    *Register(DUO_EPHY_APB(0x1Cu)) = 0x0082u;

    *Register(DUO_EPHY_PAGE_SELECT) = 0x0b00u;
    *Register(DUO_EPHY_APB(0x10u)) = 0x5252u;
    *Register(DUO_EPHY_APB(0x11u)) = 0x5252u;
    *Register(DUO_EPHY_APB(0x12u)) = 0x4B52u;
    *Register(DUO_EPHY_APB(0x13u)) = 0x3D47u;
    *Register(DUO_EPHY_APB(0x14u)) = 0xAA99u;
    *Register(DUO_EPHY_APB(0x15u)) = 0x989Eu;
    *Register(DUO_EPHY_APB(0x16u)) = 0x9395u;
    *Register(DUO_EPHY_APB(0x17u)) = 0x9091u;
    *Register(DUO_EPHY_APB(0x18u)) = 0x8E8Fu;
    *Register(DUO_EPHY_APB(0x19u)) = 0x8D8Eu;
    *Register(DUO_EPHY_APB(0x1Au)) = 0x8C8Cu;
    *Register(DUO_EPHY_APB(0x1Bu)) = 0x8B8Bu;
    *Register(DUO_EPHY_APB(0x1Cu)) = 0x008Au;

    *Register(DUO_EPHY_PAGE_SELECT) = 0x0d00u;
    *Register(DUO_EPHY_APB(0x10u)) = 0x1E0Au;
    *Register(DUO_EPHY_APB(0x11u)) = 0x3862u;
    *Register(DUO_EPHY_APB(0x12u)) = 0x1E62u;
    *Register(DUO_EPHY_APB(0x13u)) = 0x2A08u;
    *Register(DUO_EPHY_APB(0x14u)) = 0x244Cu;
    *Register(DUO_EPHY_APB(0x15u)) = 0x1A44u;
    *Register(DUO_EPHY_APB(0x16u)) = 0x061Cu;

    *Register(DUO_EPHY_PAGE_SELECT) = 0x0e00u;
    *Register(DUO_EPHY_APB(0x10u)) = 0x2D30u;
    *Register(DUO_EPHY_APB(0x11u)) = 0x3470u;
    *Register(DUO_EPHY_APB(0x12u)) = 0x0648u;
    *Register(DUO_EPHY_APB(0x13u)) = 0x261Cu;
    *Register(DUO_EPHY_APB(0x14u)) = 0x3160u;
    *Register(DUO_EPHY_APB(0x15u)) = 0x2D5Eu;

    *Register(DUO_EPHY_PAGE_SELECT) = 0x0f00u;
    *Register(DUO_EPHY_APB(0x10u)) = 0x2922u;
    *Register(DUO_EPHY_APB(0x11u)) = 0x366Eu;
    *Register(DUO_EPHY_APB(0x12u)) = 0x0752u;
    *Register(DUO_EPHY_APB(0x13u)) = 0x2556u;
    *Register(DUO_EPHY_APB(0x14u)) = 0x2348u;
    *Register(DUO_EPHY_APB(0x15u)) = 0x0C30u;

    *Register(DUO_EPHY_PAGE_SELECT) = 0x1000u;
    *Register(DUO_EPHY_APB(0x10u)) = 0x1E08u;
    *Register(DUO_EPHY_APB(0x11u)) = 0x3868u;
    *Register(DUO_EPHY_APB(0x12u)) = 0x1462u;
    *Register(DUO_EPHY_APB(0x13u)) = 0x1A0Eu;
    *Register(DUO_EPHY_APB(0x14u)) = 0x305Eu;
    *Register(DUO_EPHY_APB(0x15u)) = 0x2F62u;

    *Register(DUO_EPHY_PAGE_SELECT) = 0x0100u;
    *Register(DUO_EPHY_APB(0x1Au)) =
        *Register(DUO_EPHY_APB(0x1Au)) & ~0x0f00u;

    *Register(DUO_EPHY_PAGE_SELECT) = 0x1300u;
    *Register(DUO_EPHY_APB(0x16u)) = 0x0012u;
    *Register(DUO_EPHY_APB(0x17u)) = 0x6848u;

    /* CV181x LPF/HPF（mars） */
    *Register(DUO_EPHY_PAGE_SELECT) = 0x1200u;
    *Register(DUO_EPHY_APB(0x12u)) = 0x0808u;
    *Register(DUO_EPHY_APB(0x13u)) = 0x0808u;
    *Register(DUO_EPHY_APB(0x14u)) = 0x32f8u;
    *Register(DUO_EPHY_APB(0x15u)) = 0xf8dcu;

    *Register(DUO_EPHY_PAGE_SELECT) = 0x0000u;
    *Register(DUO_EPHY_CONTROL) = DUO_EPHY_CONTROL_AUTONEG_START;
    *Register(DUO_EPHY_APB(DUO_PHY_BASIC_CONTROL)) =
        *Register(DUO_EPHY_APB(DUO_PHY_BASIC_CONTROL)) |
        DUO_PHY_BASIC_CONTROL_FULL_DUPLEX;

    *Register(DUO_EPHY_APB_SELECT) = 0u;
    DelayMilliseconds(1u);
}

static int PhyIdentityValid(UINT16 Identifier1, UINT16 Identifier2) {
    if (Identifier1 == 0u || Identifier1 == 0xFFFFu) {
        return 0;
    }
    if (Identifier2 == 0u || Identifier2 == 0xFFFFu) {
        return 0;
    }
    return 1;
}

static int PhyFind(UINT32 *OutAddress, UINT16 *OutIdentifier1,
                   UINT16 *OutIdentifier2) {
    static const UINT32 Prefer[] = { 1u, 0u };
    UINT32 Address;
    UINT32 Index;
    UINT16 Identifier1;
    UINT16 Identifier2;

    for (Index = 0; Index < sizeof(Prefer) / sizeof(Prefer[0]); Index++) {
        Address = Prefer[Index];
        if (MiiRead(Address, DUO_PHY_IDENTIFIER_1, &Identifier1) != 0 ||
            MiiRead(Address, DUO_PHY_IDENTIFIER_2, &Identifier2) != 0) {
            continue;
        }
        if (PhyIdentityValid(Identifier1, Identifier2)) {
            *OutAddress = Address;
            *OutIdentifier1 = Identifier1;
            *OutIdentifier2 = Identifier2;
            return 0;
        }
    }
    for (Address = 0; Address < 32u; Address++) {
        if (Address == 0u || Address == 1u) {
            continue;
        }
        if (MiiRead(Address, DUO_PHY_IDENTIFIER_1, &Identifier1) != 0 ||
            MiiRead(Address, DUO_PHY_IDENTIFIER_2, &Identifier2) != 0) {
            continue;
        }
        if (PhyIdentityValid(Identifier1, Identifier2)) {
            *OutAddress = Address;
            *OutIdentifier1 = Identifier1;
            *OutIdentifier2 = Identifier2;
            return 0;
        }
    }
    return -1;
}

static int PhyReadBasicStatus(UINT32 Address, UINT16 *OutStatus) {
    UINT16 First;
    UINT16 Second;

    /* BMSR.Link 锁存：IEEE 要求读两次 */
    if (MiiRead(Address, DUO_PHY_BASIC_STATUS, &First) != 0 ||
        MiiRead(Address, DUO_PHY_BASIC_STATUS, &Second) != 0) {
        return -1;
    }
    *OutStatus = Second;
    return 0;
}

/*
 * IEEE clause 22 / Linux genphy_config_aneg：写 Advertise 再 ANENABLE|ANRESTART。
 * 不做 BMCR 软复位——会冲掉 cvitek config_init 的 ANA 页表。
 */
static int PhyRestartAutoneg(UINT32 Address) {
    UINT16 Control;

    if (MiiWrite(Address, DUO_PHY_AUTONEG_ADVERTISE,
                 DUO_PHY_ADVERTISE_DEFAULT) != 0) {
        return -1;
    }
    if (MiiRead(Address, DUO_PHY_BASIC_CONTROL, &Control) != 0) {
        return -1;
    }
    Control &= (UINT16)~DUO_PHY_BASIC_CONTROL_ISOLATE;
    Control |= (UINT16)(DUO_PHY_BASIC_CONTROL_AUTONEG_ENABLE |
                        DUO_PHY_BASIC_CONTROL_AUTONEG_RESTART |
                        DUO_PHY_BASIC_CONTROL_FULL_DUPLEX);
    return MiiWrite(Address, DUO_PHY_BASIC_CONTROL, Control);
}

static int PhyProbe(void) {
    UINT32 Address;
    UINT16 Identifier1;
    UINT16 Identifier2;
    UINT16 BasicStatus;
    UINT16 Control;
    UINT32 Poll;
    int LinkUp;

    HalSerialBootMark("Boot: Duo ETH PHY\n");
    PhyInitialize();
    PhyConfigure();

    if (PhyFind(&Address, &Identifier1, &Identifier2) != 0) {
        UINT16 Sample1;
        UINT16 Sample2;
        int ReadStatus;

        HalSerialBootMark("Boot: Duo ETH PHY none (MDIO)\n");
        ReadStatus = MiiRead(1u, DUO_PHY_IDENTIFIER_1, &Sample1);
        if (ReadStatus != 0) {
            HalSerialBootMark("Boot: Duo ETH MDIO busy/fail @1\n");
        } else {
            (void)MiiRead(1u, DUO_PHY_IDENTIFIER_2, &Sample2);
            HalSerialBootMark("Boot: Duo ETH MDIO@1 id=");
            MarkHex16(Sample1);
            MarkHex16(Sample2);
            HalSerialBootMark("\n");
        }
        return -1;
    }

    gPhyAddress = Address;
    gPhyOk = 1;
    MarkHex("Boot: Duo ETH phy=", Address);
    HalSerialBootMark(" id=");
    MarkHex16(Identifier1);
    MarkHex16(Identifier2);
    HalSerialBootMark("\n");

    if (PhyRestartAutoneg(Address) != 0) {
        HalSerialBootMark("Boot: Duo ETH AN restart fail\n");
        return -1;
    }
    if (MiiRead(Address, DUO_PHY_BASIC_CONTROL, &Control) == 0) {
        MarkHex("Boot: Duo ETH bmcr=", Control);
        HalSerialBootMark("\n");
    }

    LinkUp = 0;
    for (Poll = 0; Poll < 30u; Poll++) {
        DelayMilliseconds(100u);
        if (PhyReadBasicStatus(Address, &BasicStatus) != 0) {
            HalSerialBootMark("Boot: Duo ETH BasicStatus fail\n");
            return -1;
        }
        if ((BasicStatus & DUO_PHY_BASIC_STATUS_LINK) != 0) {
            LinkUp = 1;
            break;
        }
    }

    {
        UINT16 Advertise;
        UINT16 Partner;

        if (MiiRead(Address, DUO_PHY_AUTONEG_ADVERTISE, &Advertise) != 0) {
            Advertise = 0;
        }
        if (MiiRead(Address, DUO_PHY_LINK_PARTNER_ABILITY, &Partner) != 0) {
            Partner = 0;
        }
        MarkHex("Boot: Duo ETH bmsr=", BasicStatus);
        MarkHex(" adv=", Advertise);
        MarkHex(" lpa=", Partner);
        if ((BasicStatus & DUO_PHY_BASIC_STATUS_AUTONEG_COMPLETE) != 0) {
            HalSerialBootMark(" aneg=1");
        } else {
            HalSerialBootMark(" aneg=0");
        }
        if (LinkUp) {
            HalSerialBootMark(" link=up\n");
        } else {
            HalSerialBootMark(" link=down\n");
        }
    }
    return 0;
}

static int DmaSoftwareReset(void) {
    UINT32 Index;
    UINT32 BusMode;

    BusMode = *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_BUS_MODE);
    *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_BUS_MODE) =
        BusMode | DUO_ETHERNET_DMA_SOFTWARE_RESET;
    for (Index = 0; Index < 100000u; Index++) {
        if ((*Register(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_BUS_MODE) &
             DUO_ETHERNET_DMA_SOFTWARE_RESET) == 0u) {
            return 0;
        }
        Spin(50u);
    }
    return -1;
}

static void DmaCacheSync(void *Address, UINTN Length) {
    HalDcacheCleanInvalidateRange(Address, Length);
}

static int DmaWaitOwnClear(volatile EthernetDmaDescriptor *Descriptor) {
    UINT32 Index;

    for (Index = 0; Index < 200000u; Index++) {
        DmaCacheSync((void *)Descriptor, sizeof(*Descriptor));
        if ((Descriptor->Descriptor0 & DUO_ETHERNET_DESCRIPTOR_OWN) == 0u) {
            return 0;
        }
        Spin(50u);
    }
    return -1;
}

static int PhyLinkUp(void) {
    UINT16 BasicStatus;

    if (!gPhyOk) {
        return 0;
    }
    if (PhyReadBasicStatus(gPhyAddress, &BasicStatus) != 0) {
        return 0;
    }
    return (BasicStatus & DUO_PHY_BASIC_STATUS_LINK) != 0;
}

static void DmaProgramMacAddress(void) {
    gMac[0] = 0x02u;
    gMac[1] = 0x00u;
    gMac[2] = 0x00u;
    gMac[3] = 0x00u;
    gMac[4] = 0x00u;
    gMac[5] = 0x01u;
    *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_ADDRESS0_HIGH) =
        DUO_ETHERNET_ADDRESS_ENABLE | ((UINT32)gMac[4] << 8) | gMac[5];
    *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_ADDRESS0_LOW) =
        ((UINT32)gMac[0]) | ((UINT32)gMac[1] << 8) | ((UINT32)gMac[2] << 16) |
        ((UINT32)gMac[3] << 24);
}

static void DmaRearmReceive(UINT32 Index) {
    gReceiveDescriptor[Index].Descriptor1 =
        DUO_ETHERNET_DMA_BUFFER_SIZE & DUO_ETHERNET_RECEIVE_BUFFER1_SIZE_MASK;
    if (Index + 1u == DUO_ETHERNET_DMA_RING_COUNT) {
        gReceiveDescriptor[Index].Descriptor1 |= DUO_ETHERNET_RECEIVE_END_RING;
    }
    gReceiveDescriptor[Index].Descriptor2 =
        (UINT32)(UINTN)&gReceiveBuffer[Index][0];
    gReceiveDescriptor[Index].Descriptor3 = 0u;
    gReceiveDescriptor[Index].Descriptor0 = DUO_ETHERNET_DESCRIPTOR_OWN;
    DmaCacheSync(&gReceiveDescriptor[Index], sizeof(gReceiveDescriptor[Index]));
}

/*
 * net-3/4：normal 描述符环（本片 ENHDESSEL=0）。
 * 先 MAC LM 自检；通过后关 LM，环常驻（收发包槽）。
 */
static int DmaProbe(void) {
    UINT32 Index;
    UINT32 Feature;
    UINT32 BusMode;
    UINT32 Operation;
    UINT32 MacConfig;
    UINT32 Status;
    UINT32 FrameLength;
    UINT32 TransmitControl;

    HalSerialBootMark("Boot: Duo ETH DMA\n");

    Feature = *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_HARDWARE_FEATURE);
    MarkHex("Boot: Duo ETH dmafeat=", Feature);
    if ((Feature & DUO_ETHERNET_DMA_FEATURE_ENHANCED_DESCRIPTOR) != 0u) {
        HalSerialBootMark(" enh=1\n");
        HalSerialBootMark("Boot: Duo ETH DMA need normal (enh unsupported)\n");
        return -1;
    }
    HalSerialBootMark(" enh=0 normal\n");

    if (DmaSoftwareReset() != 0) {
        HalSerialBootMark("Boot: Duo ETH DMA reset fail\n");
        return -1;
    }

    BusMode = (8u << DUO_ETHERNET_DMA_PBL_SHIFT);
    *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_BUS_MODE) = BusMode;

    for (Index = 0; Index < DUO_ETHERNET_DMA_RING_COUNT; Index++) {
        gReceiveDescriptor[Index].Descriptor1 =
            DUO_ETHERNET_DMA_BUFFER_SIZE & DUO_ETHERNET_RECEIVE_BUFFER1_SIZE_MASK;
        if (Index + 1u == DUO_ETHERNET_DMA_RING_COUNT) {
            gReceiveDescriptor[Index].Descriptor1 |=
                DUO_ETHERNET_RECEIVE_END_RING;
        }
        gReceiveDescriptor[Index].Descriptor2 =
            (UINT32)(UINTN)&gReceiveBuffer[Index][0];
        gReceiveDescriptor[Index].Descriptor3 = 0u;
        gReceiveDescriptor[Index].Descriptor0 = DUO_ETHERNET_DESCRIPTOR_OWN;

        gTransmitDescriptor[Index].Descriptor0 = 0u;
        gTransmitDescriptor[Index].Descriptor1 = 0u;
        gTransmitDescriptor[Index].Descriptor2 = 0u;
        gTransmitDescriptor[Index].Descriptor3 = 0u;
    }
    DmaCacheSync(gReceiveDescriptor, sizeof(gReceiveDescriptor));
    DmaCacheSync(gTransmitDescriptor, sizeof(gTransmitDescriptor));
    DmaCacheSync(gReceiveBuffer, sizeof(gReceiveBuffer));

    *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_RECEIVE_LIST_BASE) =
        (UINT32)(UINTN)&gReceiveDescriptor[0];
    *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_TRANSMIT_LIST_BASE) =
        (UINT32)(UINTN)&gTransmitDescriptor[0];

    DmaProgramMacAddress();
    *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_FRAME_FILTER) =
        DUO_ETHERNET_FRAME_FILTER_PROMISCUOUS;

    MacConfig = DUO_ETHERNET_MAC_PORT_SELECT_MII | DUO_ETHERNET_MAC_SPEED_100 |
                DUO_ETHERNET_MAC_DUPLEX | DUO_ETHERNET_MAC_LOOPBACK |
                DUO_ETHERNET_MAC_TRANSMITTER_ENABLE |
                DUO_ETHERNET_MAC_RECEIVER_ENABLE;
    *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_MAC_CONFIG) = MacConfig;

    Operation = DUO_ETHERNET_DMA_TRANSMIT_STORE_FORWARD |
                DUO_ETHERNET_DMA_RECEIVE_STORE_FORWARD |
                DUO_ETHERNET_DMA_OPERATE_SECOND_FRAME |
                DUO_ETHERNET_DMA_START_TRANSMIT |
                DUO_ETHERNET_DMA_START_RECEIVE;
    *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_OPERATION_MODE) = Operation;
    *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_INTERRUPT_ENABLE) = 0u;

    /* 最小以太网帧：广播 + 本机 SA + ethertype 0x88B5 */
    for (Index = 0; Index < 64u; Index++) {
        gTransmitBuffer[0][Index] = 0u;
    }
    for (Index = 0; Index < 6u; Index++) {
        gTransmitBuffer[0][Index] = 0xFFu;
    }
    gTransmitBuffer[0][6] = 0x02u;
    gTransmitBuffer[0][12] = 0x88u;
    gTransmitBuffer[0][13] = 0xB5u;
    gTransmitBuffer[0][14] = 0x54u; /* 'T' */
    gTransmitBuffer[0][15] = 0x4Fu; /* 'O' */
    gTransmitBuffer[0][16] = 0x59u; /* 'Y' */
    DmaCacheSync(gTransmitBuffer[0], 64u);

    for (Index = 0; Index < DUO_ETHERNET_DMA_RING_COUNT; Index++) {
        gTransmitDescriptor[Index].Descriptor0 = 0u;
        gTransmitDescriptor[Index].Descriptor1 = 0u;
        gTransmitDescriptor[Index].Descriptor2 = 0u;
        gTransmitDescriptor[Index].Descriptor3 = 0u;
        if (Index + 1u == DUO_ETHERNET_DMA_RING_COUNT) {
            gTransmitDescriptor[Index].Descriptor1 =
                DUO_ETHERNET_TRANSMIT_END_RING;
        }
    }
    /* normal：FS/LS/IC/size 在 TDES1；OWN 最后写 TDES0 */
    TransmitControl = (64u & DUO_ETHERNET_TRANSMIT_BUFFER1_SIZE_MASK) |
                      DUO_ETHERNET_TRANSMIT_FIRST_SEGMENT |
                      DUO_ETHERNET_TRANSMIT_LAST_SEGMENT |
                      DUO_ETHERNET_TRANSMIT_INTERRUPT;
    gTransmitDescriptor[0].Descriptor2 =
        (UINT32)(UINTN)&gTransmitBuffer[0][0];
    gTransmitDescriptor[0].Descriptor3 = 0u;
    gTransmitDescriptor[0].Descriptor1 = TransmitControl;
    gTransmitDescriptor[0].Descriptor0 = DUO_ETHERNET_DESCRIPTOR_OWN;
    DmaCacheSync(gTransmitDescriptor, sizeof(gTransmitDescriptor));

    *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_TRANSMIT_POLL) = 1u;
    *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_RECEIVE_POLL) = 1u;

    if (DmaWaitOwnClear(&gTransmitDescriptor[0]) != 0) {
        Status = *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_STATUS);
        MarkHex("Boot: Duo ETH DMA tx timeout st=", Status);
        HalSerialBootMark("\n");
        return -1;
    }
    HalSerialBootMark("Boot: Duo ETH DMA tx=ok\n");

    if (DmaWaitOwnClear(&gReceiveDescriptor[0]) != 0) {
        Status = *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_STATUS);
        MarkHex("Boot: Duo ETH DMA rx timeout st=", Status);
        HalSerialBootMark("\n");
        return -1;
    }

    DmaCacheSync(&gReceiveDescriptor[0], sizeof(gReceiveDescriptor[0]));
    DmaCacheSync(gReceiveBuffer[0], DUO_ETHERNET_DMA_BUFFER_SIZE);
    if ((gReceiveDescriptor[0].Descriptor0 &
         DUO_ETHERNET_RECEIVE_DESCRIPTOR_ERROR_SUMMARY) != 0u) {
        MarkHex("Boot: Duo ETH DMA rx err des0=",
                gReceiveDescriptor[0].Descriptor0);
        HalSerialBootMark("\n");
        return -1;
    }
    FrameLength =
        (gReceiveDescriptor[0].Descriptor0 >>
         DUO_ETHERNET_RECEIVE_DESCRIPTOR_FRAME_LENGTH_SHIFT) &
        DUO_ETHERNET_RECEIVE_DESCRIPTOR_FRAME_LENGTH_MASK;
    MarkHex("Boot: Duo ETH DMA rx=ok len=", FrameLength);
    HalSerialBootMark("\n");

    /*
     * LM 后整环重拉线缆模式（对表 U-Boot designware_eth_init）：
     * 停 TE/RE→DMA 复位→环→按 PHY LPA 写 MAC→开 SF/Flush→TE/RE。
     * 只清 LM 位不够：协商速度/双工与环回残留会让线缆收发静默失败。
     */
    return DmaStartWire();
}

static void MacAdjustFromPhy(void) {
    UINT16 Partner = 0;
    UINT32 MacConfig;
    int FullDuplex = 1;
    int Speed100 = 1;

    if (gPhyOk &&
        MiiRead(gPhyAddress, DUO_PHY_LINK_PARTNER_ABILITY, &Partner) == 0) {
        if ((Partner & DUO_PHY_ADVERTISE_100_FULL) != 0) {
            Speed100 = 1;
            FullDuplex = 1;
        } else if ((Partner & DUO_PHY_ADVERTISE_100_HALF) != 0) {
            Speed100 = 1;
            FullDuplex = 0;
        } else if ((Partner & DUO_PHY_ADVERTISE_10_FULL) != 0) {
            Speed100 = 0;
            FullDuplex = 1;
        } else if ((Partner & DUO_PHY_ADVERTISE_10_HALF) != 0) {
            Speed100 = 0;
            FullDuplex = 0;
        }
    }

    MacConfig = DUO_ETHERNET_MAC_PORT_SELECT_MII | DUO_ETHERNET_MAC_FRAME_BURST |
                DUO_ETHERNET_MAC_DISABLE_RECEIVE_OWN;
    if (Speed100) {
        MacConfig |= DUO_ETHERNET_MAC_SPEED_100;
    }
    if (FullDuplex) {
        MacConfig |= DUO_ETHERNET_MAC_DUPLEX;
    }
    MacConfig |= DUO_ETHERNET_MAC_TRANSMITTER_ENABLE |
                 DUO_ETHERNET_MAC_RECEIVER_ENABLE;
    *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_MAC_CONFIG) = MacConfig;
    MarkHex("Boot: Duo ETH mac=", MacConfig);
    HalSerialBootMark(Speed100 ? " 100" : " 10");
    HalSerialBootMark(FullDuplex ? "/FD\n" : "/HD\n");
}

static int DmaStartWire(void) {
    UINT32 Index;
    UINT32 Feature;
    UINT32 BusMode;
    UINT32 Operation;
    UINT32 Status;

    HalSerialBootMark("Boot: Duo ETH DMA wire\n");

    /* 停 MAC / DMA */
    *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_MAC_CONFIG) = 0u;
    Operation = *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_OPERATION_MODE);
    Operation &= ~(DUO_ETHERNET_DMA_START_TRANSMIT |
                   DUO_ETHERNET_DMA_START_RECEIVE);
    *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_OPERATION_MODE) = Operation;

    Feature = *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_HARDWARE_FEATURE);
    if ((Feature & DUO_ETHERNET_DMA_FEATURE_ENHANCED_DESCRIPTOR) != 0u) {
        return -1;
    }
    if (DmaSoftwareReset() != 0) {
        HalSerialBootMark("Boot: Duo ETH DMA wire reset fail\n");
        return -1;
    }

    BusMode = (8u << DUO_ETHERNET_DMA_PBL_SHIFT);
    *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_BUS_MODE) = BusMode;

    for (Index = 0; Index < DUO_ETHERNET_DMA_RING_COUNT; Index++) {
        DmaRearmReceive(Index);
        gTransmitDescriptor[Index].Descriptor0 = 0u;
        gTransmitDescriptor[Index].Descriptor1 =
            (Index + 1u == DUO_ETHERNET_DMA_RING_COUNT)
                ? DUO_ETHERNET_TRANSMIT_END_RING
                : 0u;
        gTransmitDescriptor[Index].Descriptor2 = 0u;
        gTransmitDescriptor[Index].Descriptor3 = 0u;
    }
    DmaCacheSync(gTransmitDescriptor, sizeof(gTransmitDescriptor));
    DmaCacheSync(gReceiveDescriptor, sizeof(gReceiveDescriptor));

    *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_RECEIVE_LIST_BASE) =
        (UINT32)(UINTN)&gReceiveDescriptor[0];
    *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_TRANSMIT_LIST_BASE) =
        (UINT32)(UINTN)&gTransmitDescriptor[0];

    DmaProgramMacAddress();
    *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_FRAME_FILTER) =
        DUO_ETHERNET_FRAME_FILTER_PROMISCUOUS;

    Operation = DUO_ETHERNET_DMA_TRANSMIT_STORE_FORWARD |
                DUO_ETHERNET_DMA_RECEIVE_STORE_FORWARD |
                DUO_ETHERNET_DMA_OPERATE_SECOND_FRAME |
                DUO_ETHERNET_DMA_FLUSH_TRANSMIT_FIFO;
    *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_OPERATION_MODE) = Operation;
    Operation |= DUO_ETHERNET_DMA_START_TRANSMIT |
                 DUO_ETHERNET_DMA_START_RECEIVE;
    *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_OPERATION_MODE) = Operation;
    *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_INTERRUPT_ENABLE) = 0u;

    MacAdjustFromPhy();

    gTransmitIndex = 0u;
    gReceiveIndex = 0u;
    gTransmitDone = 0u;
    gReceiveFrames = 0u;
    Status = *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_STATUS);
    *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_STATUS) = Status;
    *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_RECEIVE_POLL) = 1u;
    gDmaOk = 1;
    HalSerialBootMark("Boot: Duo ETH DMA wire ready\n");
    return 0;
}

int BoardEthernetSendFrame(const UINT8 *Frame, UINTN FrameLength) {
    UINT32 Index;
    UINT32 Length;
    UINT32 TransmitControl;
    volatile EthernetDmaDescriptor *Descriptor;

    if (!gDmaOk || Frame == 0 || FrameLength < 14u) {
        return -1;
    }
    Length = (UINT32)FrameLength;
    if (Length < 60u) {
        Length = 60u;
    }
    if (Length > DUO_ETHERNET_DMA_BUFFER_SIZE) {
        return -1;
    }
    Index = gTransmitIndex;
    Descriptor = &gTransmitDescriptor[Index];
    DmaCacheSync((void *)Descriptor, sizeof(*Descriptor));
    if ((Descriptor->Descriptor0 & DUO_ETHERNET_DESCRIPTOR_OWN) != 0u) {
        return -1;
    }
    for (TransmitControl = 0; TransmitControl < Length; TransmitControl++) {
        gTransmitBuffer[Index][TransmitControl] = 0u;
    }
    MemoryCopy(gTransmitBuffer[Index], Frame, FrameLength);
    DmaCacheSync(gTransmitBuffer[Index], Length);
    TransmitControl = (Length & DUO_ETHERNET_TRANSMIT_BUFFER1_SIZE_MASK) |
                      DUO_ETHERNET_TRANSMIT_FIRST_SEGMENT |
                      DUO_ETHERNET_TRANSMIT_LAST_SEGMENT |
                      DUO_ETHERNET_TRANSMIT_INTERRUPT;
    if (Index + 1u == DUO_ETHERNET_DMA_RING_COUNT) {
        TransmitControl |= DUO_ETHERNET_TRANSMIT_END_RING;
    }
    Descriptor->Descriptor2 = (UINT32)(UINTN)&gTransmitBuffer[Index][0];
    Descriptor->Descriptor3 = 0u;
    Descriptor->Descriptor1 = TransmitControl;
    Descriptor->Descriptor0 = DUO_ETHERNET_DESCRIPTOR_OWN;
    DmaCacheSync((void *)Descriptor, sizeof(*Descriptor));
    *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_TRANSMIT_POLL) = 1u;
    gTransmitIndex = (Index + 1u) % DUO_ETHERNET_DMA_RING_COUNT;
    return 0;
}

static int ArpLookup(UINT32 Ip, UINT8 Mac[6]) {
    UINT32 Index;

    for (Index = 0; Index < ETHERNET_ARP_CACHE_SIZE; Index++) {
        if (gArpCache[Index].Valid && gArpCache[Index].Ip == Ip) {
            MemoryCopy(Mac, gArpCache[Index].Mac, 6);
            return 1;
        }
    }
    return 0;
}

static void ArpLearn(UINT32 Ip, const UINT8 Mac[6]) {
    UINT32 Index;
    UINT32 Slot = 0;

    for (Index = 0; Index < ETHERNET_ARP_CACHE_SIZE; Index++) {
        if (gArpCache[Index].Valid && gArpCache[Index].Ip == Ip) {
            MemoryCopy(gArpCache[Index].Mac, Mac, 6);
            return;
        }
        if (!gArpCache[Index].Valid) {
            Slot = Index;
            break;
        }
        Slot = Index;
    }
    gArpCache[Slot].Ip = Ip;
    MemoryCopy(gArpCache[Slot].Mac, Mac, 6);
    gArpCache[Slot].Valid = 1;
}

static void EthernetSendArpRequest(UINT32 TargetIp) {
    UINT8 Frame[ETHERNET_ETH_HEADER_LENGTH + sizeof(EthernetArpPacket)];
    EthernetEthHeader *Eth = (EthernetEthHeader *)Frame;
    EthernetArpPacket *Arp =
        (EthernetArpPacket *)(Frame + ETHERNET_ETH_HEADER_LENGTH);

    MemorySet(Frame, 0, sizeof(Frame));
    MemorySet(Eth->Destination, 0xFF, 6);
    MemoryCopy(Eth->Source, gMac, 6);
    Eth->EtherType = ByteSwap16(ETHERNET_ETH_TYPE_ARP);
    Arp->HardwareType = ByteSwap16(1);
    Arp->ProtocolType = ByteSwap16(ETHERNET_ETH_TYPE_IP);
    Arp->HardwareLength = 6;
    Arp->ProtocolLength = 4;
    Arp->Operation = ByteSwap16(ETHERNET_ARP_OPERATION_REQUEST);
    MemoryCopy(Arp->SenderMac, gMac, 6);
    Arp->SenderIp = ByteSwap32(gIp);
    MemorySet(Arp->TargetMac, 0, 6);
    Arp->TargetIp = ByteSwap32(TargetIp);
    (void)BoardEthernetSendFrame(Frame, sizeof(Frame));
}

static void EthernetHandleArp(const EthernetArpPacket *Arp) {
    UINT16 Operation = ByteSwap16(Arp->Operation);
    UINT32 SenderIp = ByteSwap32(Arp->SenderIp);

    if (Operation == ETHERNET_ARP_OPERATION_REPLY ||
        Operation == ETHERNET_ARP_OPERATION_REQUEST) {
        ArpLearn(SenderIp, Arp->SenderMac);
    }
    if (Operation == ETHERNET_ARP_OPERATION_REQUEST &&
        ByteSwap32(Arp->TargetIp) == gIp) {
        UINT8 Frame[ETHERNET_ETH_HEADER_LENGTH + sizeof(EthernetArpPacket)];
        EthernetEthHeader *Eth = (EthernetEthHeader *)Frame;
        EthernetArpPacket *Reply =
            (EthernetArpPacket *)(Frame + ETHERNET_ETH_HEADER_LENGTH);

        MemorySet(Frame, 0, sizeof(Frame));
        MemoryCopy(Eth->Destination, Arp->SenderMac, 6);
        MemoryCopy(Eth->Source, gMac, 6);
        Eth->EtherType = ByteSwap16(ETHERNET_ETH_TYPE_ARP);
        Reply->HardwareType = ByteSwap16(1);
        Reply->ProtocolType = ByteSwap16(ETHERNET_ETH_TYPE_IP);
        Reply->HardwareLength = 6;
        Reply->ProtocolLength = 4;
        Reply->Operation = ByteSwap16(ETHERNET_ARP_OPERATION_REPLY);
        MemoryCopy(Reply->SenderMac, gMac, 6);
        Reply->SenderIp = ByteSwap32(gIp);
        MemoryCopy(Reply->TargetMac, Arp->SenderMac, 6);
        Reply->TargetIp = Arp->SenderIp;
        (void)BoardEthernetSendFrame(Frame, sizeof(Frame));
    }
}

static void EthernetHandleIcmp(const EthernetIpHeader *Ip, const UINT8 *Payload,
                               UINTN PayloadLength, const UINT8 *SourceMac) {
    const EthernetIcmpHeader *Icmp;
    UINT32 SourceIp;

    if (PayloadLength < ETHERNET_ICMP_HEADER_LENGTH) {
        return;
    }
    Icmp = (const EthernetIcmpHeader *)Payload;
    SourceIp = ByteSwap32(Ip->Source);
    if (Icmp->Type == ETHERNET_ICMP_ECHO_REPLY) {
        if (gPingWait && SourceIp == gPingTarget &&
            ByteSwap16(Icmp->Id) == gPingId) {
            gPingWait = 0;
        }
        return;
    }
    if (Icmp->Type == ETHERNET_ICMP_ECHO_REQUEST &&
        ByteSwap32(Ip->Destination) == gIp && SourceMac != 0) {
        UINT8 Frame[ETHERNET_ETH_HEADER_LENGTH + ETHERNET_IP_HEADER_LENGTH +
                     1500u];
        EthernetEthHeader *Eth = (EthernetEthHeader *)Frame;
        EthernetIpHeader *OutIp =
            (EthernetIpHeader *)(Frame + ETHERNET_ETH_HEADER_LENGTH);
        EthernetIcmpHeader *OutIcmp =
            (EthernetIcmpHeader *)(Frame + ETHERNET_ETH_HEADER_LENGTH +
                                   ETHERNET_IP_HEADER_LENGTH);
        UINT16 Total = (UINT16)(ETHERNET_IP_HEADER_LENGTH + PayloadLength);
        UINT16 Sum;

        if (PayloadLength >
            sizeof(Frame) - ETHERNET_ETH_HEADER_LENGTH -
                ETHERNET_IP_HEADER_LENGTH) {
            return;
        }
        MemorySet(Frame, 0, sizeof(Frame));
        MemoryCopy(Eth->Destination, SourceMac, 6);
        MemoryCopy(Eth->Source, gMac, 6);
        Eth->EtherType = ByteSwap16(ETHERNET_ETH_TYPE_IP);
        OutIp->VersionIhl = 0x45u;
        OutIp->TimeToLive = 64u;
        OutIp->Protocol = ETHERNET_IP_PROTOCOL_ICMP;
        OutIp->Source = ByteSwap32(gIp);
        OutIp->Destination = Ip->Source;
        OutIp->TotalLength = ByteSwap16(Total);
        OutIp->Identification = ByteSwap16(0x4F53u);
        OutIp->Checksum = 0;
        Sum = EthernetChecksum(OutIp, ETHERNET_IP_HEADER_LENGTH);
        OutIp->Checksum = ByteSwap16(Sum);
        MemoryCopy(OutIcmp, Payload, PayloadLength);
        OutIcmp->Type = ETHERNET_ICMP_ECHO_REPLY;
        OutIcmp->Checksum = 0;
        Sum = EthernetChecksum(OutIcmp, PayloadLength);
        OutIcmp->Checksum = ByteSwap16(Sum);
        (void)BoardEthernetSendFrame(Frame, ETHERNET_ETH_HEADER_LENGTH + Total);
    }
}

static void EthernetHandleFrame(const UINT8 *Frame, UINTN FrameLength) {
    const EthernetEthHeader *Eth;
    UINT16 EtherType;

    if (Frame == 0 || FrameLength < ETHERNET_ETH_HEADER_LENGTH) {
        return;
    }
    /* dwmac 描述符长度常含 FCS；剥 4 再解析 */
    if (FrameLength >= ETHERNET_ETH_HEADER_LENGTH + 4u) {
        FrameLength -= 4u;
    }
#ifdef LWIP_ENABLE
    if (gLwIpReceive) {
        LwIpNetifInput(Frame, FrameLength);
        return;
    }
#endif
    Eth = (const EthernetEthHeader *)Frame;
    EtherType = ByteSwap16(Eth->EtherType);
    if (EtherType == ETHERNET_ETH_TYPE_ARP) {
        if (FrameLength >=
            ETHERNET_ETH_HEADER_LENGTH + sizeof(EthernetArpPacket)) {
            EthernetHandleArp(
                (const EthernetArpPacket *)(Frame + ETHERNET_ETH_HEADER_LENGTH));
        }
        return;
    }
    if (EtherType == ETHERNET_ETH_TYPE_IP) {
        const EthernetIpHeader *Ip;
        UINT16 Total;
        UINTN HeaderLength;
        UINTN PayloadLength;

        if (FrameLength <
            ETHERNET_ETH_HEADER_LENGTH + ETHERNET_IP_HEADER_LENGTH) {
            return;
        }
        Ip = (const EthernetIpHeader *)(Frame + ETHERNET_ETH_HEADER_LENGTH);
        HeaderLength = (UINTN)(Ip->VersionIhl & 0x0Fu) * 4u;
        if (HeaderLength < ETHERNET_IP_HEADER_LENGTH) {
            return;
        }
        Total = ByteSwap16(Ip->TotalLength);
        if (Total < HeaderLength) {
            return;
        }
        PayloadLength = (UINTN)Total - HeaderLength;
        if (ETHERNET_ETH_HEADER_LENGTH + HeaderLength + PayloadLength >
            FrameLength) {
            return;
        }
        if (Ip->Protocol == ETHERNET_IP_PROTOCOL_ICMP) {
            EthernetHandleIcmp(Ip,
                               Frame + ETHERNET_ETH_HEADER_LENGTH + HeaderLength,
                               PayloadLength, Eth->Source);
        }
    }
}

static void EthernetPoll(void) {
    UINT32 Index;
    UINT32 Guard;

    if (!gDmaOk) {
        return;
    }
    for (Index = 0; Index < DUO_ETHERNET_DMA_RING_COUNT; Index++) {
        volatile EthernetDmaDescriptor *Descriptor = &gTransmitDescriptor[Index];

        DmaCacheSync((void *)Descriptor, sizeof(*Descriptor));
        if ((Descriptor->Descriptor0 & DUO_ETHERNET_DESCRIPTOR_OWN) != 0u) {
            continue;
        }
        if (Descriptor->Descriptor2 != 0u) {
            gTransmitDone++;
            Descriptor->Descriptor2 = 0u;
            Descriptor->Descriptor1 =
                (Index + 1u == DUO_ETHERNET_DMA_RING_COUNT)
                    ? DUO_ETHERNET_TRANSMIT_END_RING
                    : 0u;
            DmaCacheSync((void *)Descriptor, sizeof(*Descriptor));
        }
    }
    for (Guard = 0; Guard < DUO_ETHERNET_DMA_RING_COUNT; Guard++) {
        UINT32 ReceiveIndex = gReceiveIndex;
        volatile EthernetDmaDescriptor *Descriptor =
            &gReceiveDescriptor[ReceiveIndex];
        UINT32 Descriptor0;
        UINT32 FrameLength;

        DmaCacheSync((void *)Descriptor, sizeof(*Descriptor));
        Descriptor0 = Descriptor->Descriptor0;
        if ((Descriptor0 & DUO_ETHERNET_DESCRIPTOR_OWN) != 0u) {
            break;
        }
        FrameLength = (Descriptor0 >>
                       DUO_ETHERNET_RECEIVE_DESCRIPTOR_FRAME_LENGTH_SHIFT) &
                      DUO_ETHERNET_RECEIVE_DESCRIPTOR_FRAME_LENGTH_MASK;
        if ((Descriptor0 & DUO_ETHERNET_RECEIVE_DESCRIPTOR_ERROR_SUMMARY) ==
                0u &&
            FrameLength >= ETHERNET_ETH_HEADER_LENGTH) {
            gReceiveFrames++;
            DmaCacheSync(gReceiveBuffer[ReceiveIndex], FrameLength);
            EthernetHandleFrame(gReceiveBuffer[ReceiveIndex], FrameLength);
        }
        DmaRearmReceive(ReceiveIndex);
        gReceiveIndex = (ReceiveIndex + 1u) % DUO_ETHERNET_DMA_RING_COUNT;
        *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_DMA_RECEIVE_POLL) = 1u;
    }
}

static int EthernetResolve(UINT32 TargetIp, UINT8 Mac[6], int TimeoutMilliseconds) {
    int Tries = TimeoutMilliseconds > 0 ? TimeoutMilliseconds / 10 : 100;

    if (ArpLookup(TargetIp, Mac)) {
        return 0;
    }
    EthernetSendArpRequest(TargetIp);
    while (Tries-- > 0) {
        EthernetPoll();
        if (ArpLookup(TargetIp, Mac)) {
            return 0;
        }
        HalCpuHalt();
    }
    return -1;
}

static int EthernetReady(void) {
    return gBound && gDmaOk && PhyLinkUp();
}

static void EthernetGetMac(UINT8 Mac[6]) {
    if (Mac) {
        MemoryCopy(Mac, gMac, 6);
    }
}

static UINT32 EthernetGetIp(void) {
    return gIp;
}

static void EthernetSetIp(UINT32 Ip) {
    gIp = Ip;
}

static void EthernetFormatIp(UINT32 Ip, char *Buffer, int BufferLength) {
    if (!Buffer || BufferLength < 16) {
        return;
    }
    FormatDottedDecimal(Ip, Buffer, BufferLength);
}

static int EthernetParseIp(const char *Text, UINT32 *Ip) {
    UINT32 Parts[4];
    int Part = 0;
    UINT32 Value = 0;
    int Digits = 0;

    if (Text == 0 || Ip == 0) {
        return -1;
    }
    while (*Text) {
        if (*Text >= '0' && *Text <= '9') {
            Value = Value * 10u + (UINT32)(*Text - '0');
            if (Value > 255u) {
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

static UINT16 EthernetChecksum(const void *Data, UINTN Length) {
    const UINT8 *Bytes = (const UINT8 *)Data;
    UINT32 Sum = 0;
    UINTN Index;

    for (Index = 0; Index + 1u < Length; Index += 2u) {
        Sum += ((UINT32)Bytes[Index] << 8) | Bytes[Index + 1u];
    }
    if (Index < Length) {
        Sum += (UINT32)Bytes[Index] << 8;
    }
    while (Sum >> 16) {
        Sum = (Sum & 0xFFFFu) + (Sum >> 16);
    }
    return (UINT16)~Sum;
}

static void EthernetGetStats(UINT32 *TransmitDone, UINT32 *ReceiveFrames) {
    if (TransmitDone) {
        *TransmitDone = gTransmitDone;
    }
    if (ReceiveFrames) {
        *ReceiveFrames = gReceiveFrames;
    }
}

static void EthernetSetLwIpReceive(int Enable) {
    gLwIpReceive = Enable ? 1 : 0;
}

/*
 * net-5：SendIp / Ping 经 ARP；对齐 Virtio Protocol.c。
 */
static int EthernetSendIp(UINT32 DestinationIp, UINT8 Protocol,
                          const void *Payload, UINTN PayloadLength) {
    UINT8 DestinationMac[6];
    UINT8 Frame[ETHERNET_ETH_HEADER_LENGTH + ETHERNET_IP_HEADER_LENGTH + 1400u];
    EthernetEthHeader *Eth;
    EthernetIpHeader *Ip;
    UINT16 Total;
    UINT16 Sum;

    if (!EthernetReady() || gLwIpReceive || Payload == 0 ||
        PayloadLength > 1400u) {
        return -1;
    }
    if (EthernetResolve(DestinationIp, DestinationMac, 2000) != 0) {
        return -2;
    }
    Total = (UINT16)(ETHERNET_IP_HEADER_LENGTH + PayloadLength);
    MemorySet(Frame, 0, sizeof(Frame));
    Eth = (EthernetEthHeader *)Frame;
    MemoryCopy(Eth->Destination, DestinationMac, 6);
    MemoryCopy(Eth->Source, gMac, 6);
    Eth->EtherType = ByteSwap16(ETHERNET_ETH_TYPE_IP);
    Ip = (EthernetIpHeader *)(Frame + ETHERNET_ETH_HEADER_LENGTH);
    Ip->VersionIhl = 0x45u;
    Ip->TimeToLive = 64u;
    Ip->Protocol = Protocol;
    Ip->Source = ByteSwap32(gIp);
    Ip->Destination = ByteSwap32(DestinationIp);
    Ip->TotalLength = ByteSwap16(Total);
    Ip->Identification = ByteSwap16(0x4F53u);
    Ip->Checksum = 0;
    Sum = EthernetChecksum(Ip, ETHERNET_IP_HEADER_LENGTH);
    Ip->Checksum = ByteSwap16(Sum);
    MemoryCopy(Frame + ETHERNET_ETH_HEADER_LENGTH + ETHERNET_IP_HEADER_LENGTH,
               Payload, PayloadLength);
    return BoardEthernetSendFrame(Frame, ETHERNET_ETH_HEADER_LENGTH + Total);
}

static int EthernetPing(const char *Host, int TimeoutMilliseconds) {
    UINT32 Target;
    UINT8 DestinationMac[6];
    UINT8 Frame[ETHERNET_ETH_HEADER_LENGTH + ETHERNET_IP_HEADER_LENGTH +
                ETHERNET_ICMP_HEADER_LENGTH + ETHERNET_PING_PAYLOAD_LENGTH];
    EthernetEthHeader *Eth;
    EthernetIpHeader *Ip;
    EthernetIcmpHeader *Icmp;
    UINT8 *Payload;
    UINT16 Total;
    UINT16 Sum;
    int Tries;
    UINTN Index;

    if (!EthernetReady() || gLwIpReceive) {
        return -1;
    }
    if (EthernetParseIp(Host, &Target) != 0) {
        return -1;
    }
    if (EthernetResolve(Target, DestinationMac, TimeoutMilliseconds) != 0) {
        return -1;
    }
    MemorySet(Frame, 0, sizeof(Frame));
    Eth = (EthernetEthHeader *)Frame;
    MemoryCopy(Eth->Destination, DestinationMac, 6);
    MemoryCopy(Eth->Source, gMac, 6);
    Eth->EtherType = ByteSwap16(ETHERNET_ETH_TYPE_IP);
    Ip = (EthernetIpHeader *)(Frame + ETHERNET_ETH_HEADER_LENGTH);
    Ip->VersionIhl = 0x45u;
    Ip->TimeToLive = 64u;
    Ip->Protocol = ETHERNET_IP_PROTOCOL_ICMP;
    Ip->Source = ByteSwap32(gIp);
    Ip->Destination = ByteSwap32(Target);
    Total = ETHERNET_IP_HEADER_LENGTH + ETHERNET_ICMP_HEADER_LENGTH +
            ETHERNET_PING_PAYLOAD_LENGTH;
    Ip->TotalLength = ByteSwap16(Total);
    Ip->Identification = ByteSwap16(0x1234u);
    Ip->Checksum = 0;
    Sum = EthernetChecksum(Ip, ETHERNET_IP_HEADER_LENGTH);
    Ip->Checksum = ByteSwap16(Sum);
    Icmp = (EthernetIcmpHeader *)(Frame + ETHERNET_ETH_HEADER_LENGTH +
                                  ETHERNET_IP_HEADER_LENGTH);
    gPingSequence++;
    Icmp->Type = ETHERNET_ICMP_ECHO_REQUEST;
    Icmp->Code = 0;
    Icmp->Id = ByteSwap16(gPingId);
    Icmp->Sequence = ByteSwap16(gPingSequence);
    Payload = Frame + ETHERNET_ETH_HEADER_LENGTH + ETHERNET_IP_HEADER_LENGTH +
              ETHERNET_ICMP_HEADER_LENGTH;
    for (Index = 0; Index < ETHERNET_PING_PAYLOAD_LENGTH; Index++) {
        Payload[Index] = (UINT8)Index;
    }
    Icmp->Checksum = 0;
    Sum = EthernetChecksum(Icmp, ETHERNET_ICMP_HEADER_LENGTH +
                                     ETHERNET_PING_PAYLOAD_LENGTH);
    Icmp->Checksum = ByteSwap16(Sum);
    gPingTarget = Target;
    gPingWait = 1;
    if (BoardEthernetSendFrame(Frame, ETHERNET_ETH_HEADER_LENGTH + Total) != 0) {
        gPingWait = 0;
        return -2;
    }
    Tries = TimeoutMilliseconds > 0 ? TimeoutMilliseconds / 10 : 300;
    while (Tries-- > 0 && gPingWait) {
        EthernetPoll();
        HalCpuHalt();
    }
    if (gPingWait) {
        gPingWait = 0;
        return -3;
    }
    return 0;
}

static const NET_BACKEND gEthernetBackend = {
    .Ready = EthernetReady,
    .Poll = EthernetPoll,
    .GetMac = EthernetGetMac,
    .GetIp = EthernetGetIp,
    .SetIp = EthernetSetIp,
    .FormatIp = EthernetFormatIp,
    .ParseIp = EthernetParseIp,
    .Ping = EthernetPing,
    .GetStats = EthernetGetStats,
    .SendIp = EthernetSendIp,
    .Checksum = EthernetChecksum,
    .SetLwIpRx = EthernetSetLwIpReceive,
};

static int EthernetDriverProbe(const DRIVER *Self, void *BusContext,
                               void **OutPrivate) {
    (void)Self;
    (void)BusContext;
    if (gDmaOk) {
        if (OutPrivate) {
            *OutPrivate = 0;
        }
        return 0;
    }
    if (BoardEthernetProbe() != 0 || !gDmaOk) {
        return -1;
    }
    if (OutPrivate) {
        *OutPrivate = 0;
    }
    return 0;
}

static int EthernetDriverBind(DRIVER_INSTANCE *Instance) {
    (void)Instance;
    if (!gDmaOk) {
        return -1;
    }
    if (DriverNetAttach(&gEthernetBackend) != 0) {
        return -1;
    }
    gBound = 1;
    HalNetNicEpochBump();
    HalSerialBootMark("Boot: Duo ETH bind\n");
    return 0;
}

static void EthernetDriverRemove(DRIVER_INSTANCE *Instance) {
    (void)Instance;
    gBound = 0;
}

static const DRIVER gEthernetDriver = {
    .Name = "cv181x-ethernet",
    .Class = DRIVER_CLASS_NET,
    .Match = 0,
    .Probe = EthernetDriverProbe,
    .Bind = EthernetDriverBind,
    .Remove = EthernetDriverRemove,
};

void BoardEthernetRegister(void) {
    (void)DriverRegister(&gEthernetDriver);
}

int BoardEthernetGetLink(int *Up, UINT32 *Megabits, int *FullDuplex) {
    UINT16 Partner;
    int Link;

    if (!gPhyOk) {
        return 0;
    }
    Link = PhyLinkUp();
    if (Up) {
        *Up = Link;
    }
    if (Megabits) {
        *Megabits = 0;
    }
    if (FullDuplex) {
        *FullDuplex = 0;
    }
    if (!Link) {
        return 1;
    }
    if (MiiRead(gPhyAddress, DUO_PHY_LINK_PARTNER_ABILITY, &Partner) != 0) {
        Partner = 0;
    }
    if ((Partner & DUO_PHY_ADVERTISE_100_FULL) != 0) {
        if (Megabits) {
            *Megabits = 100u;
        }
        if (FullDuplex) {
            *FullDuplex = 1;
        }
    } else if ((Partner & DUO_PHY_ADVERTISE_100_HALF) != 0) {
        if (Megabits) {
            *Megabits = 100u;
        }
    } else if ((Partner & DUO_PHY_ADVERTISE_10_FULL) != 0) {
        if (Megabits) {
            *Megabits = 10u;
        }
        if (FullDuplex) {
            *FullDuplex = 1;
        }
    } else if ((Partner & DUO_PHY_ADVERTISE_10_HALF) != 0) {
        if (Megabits) {
            *Megabits = 10u;
        }
    }
    return 1;
}

int BoardEthernetProbe(void) {
    UINT32 SoftReset;
    UINT32 ClockEnableBefore;
    UINT32 ClockEnableAfter;
    UINT32 Divider;
    UINT32 Bypass;
    UINT32 UsbPhy;
    UINT32 MacConfig;
    UINT32 Version20;
    UINT32 Version110;
    UINT32 Version;

    HalSerialBootMark("Boot: Duo ETH probe\n");

    UsbPhy = *Register(0x03000048ULL);
    SoftReset = *Register(DUO_ETHERNET_TOP_SOFT_RESET);
    ClockEnableBefore = *Register(DUO_CLKGEN_BASE + DUO_CLK_ENABLE_0);
    Divider = *Register(DUO_CLKGEN_BASE + DUO_CLK_DIVIDER_500M_ETH0);
    Bypass = *Register(DUO_CLKGEN_BASE + DUO_CLK_BYPASS_0);

    MarkHex("Boot: Duo ETH topusb=", UsbPhy);
    MarkHex(" softrst=", SoftReset);
    MarkHex(" clken0=", ClockEnableBefore);
    MarkHex(" div500=", Divider);
    MarkHex(" byp0=", Bypass);
    HalSerialBootMark("\n");

    SoftReset |= DUO_ETHERNET_SOFT_RESET_BIT;
    *Register(DUO_ETHERNET_TOP_SOFT_RESET) = SoftReset;
    Spin(8000u);

    ClockEnableAfter =
        ClockEnableBefore | DUO_CLK_ENABLE_500M_ETH0 | DUO_CLK_ENABLE_AXI4_ETH0;
    *Register(DUO_CLKGEN_BASE + DUO_CLK_ENABLE_0) = ClockEnableAfter;
    if ((Divider & 0x000F0000u) == 0u) {
        Divider = (3u << 16) | (1u << 8);
        *Register(DUO_CLKGEN_BASE + DUO_CLK_DIVIDER_500M_ETH0) = Divider;
    }
    Bypass &= ~(1u << 9);
    *Register(DUO_CLKGEN_BASE + DUO_CLK_BYPASS_0) = Bypass;
    Spin(8000u);

    ClockEnableAfter = *Register(DUO_CLKGEN_BASE + DUO_CLK_ENABLE_0);
    MacConfig = *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_MAC_CONFIG);
    Version20 = *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_VERSION);
    Version110 = *Register(DUO_ETHERNET_BASE + DUO_ETHERNET_GMAC4_VERSION);

    MarkHex("Boot: Duo ETH clken0'=", ClockEnableAfter);
    MarkHex(" mac0=", MacConfig);
    MarkHex(" ver20=", Version20);
    MarkHex(" ver110=", Version110);
    HalSerialBootMark("\n");

    if (VersionIsLive(Version20)) {
        Version = Version20;
        HalSerialBootMark("Boot: Duo ETH id=3.70a@20 ver=");
    } else if (VersionIsLive(Version110)) {
        Version = Version110;
        HalSerialBootMark("Boot: Duo ETH id=EQOS@110 ver=");
    } else {
        HalSerialBootMark("Boot: Duo ETH VERSION dead\n");
        return -1;
    }
    {
        char Buffer[9];
        HalSerialFormatHex(Buffer, Version, 8);
        HalSerialBootMark(Buffer);
    }
    HalSerialBootMark(" (MAC ok)\n");
    gMacOk = 1;

    if (PhyProbe() != 0) {
        return -1;
    }
    if (DmaProbe() != 0) {
        return -1;
    }
    return 0;
}
