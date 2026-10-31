/*
 * WifiNetPrivate.h — PR-36：数据通路（通用以太网 TX）+ DHCP/ARP 的帧格式与内部声明
 *
 * 用户眼里：`wififw dhcp` 打出来的 DISCOVER/OFFER/REQUEST/ACK、最终 IP，以及随后
 *           `WiFi: arp: reply …`（双向数据通路活了的证据）。
 * 从哪读：SdhciWifiDhcp（WifiDhcp.c，顺序）→ DhcpBuildDiscover / DhcpParseReply
 *         （WifiDhcpMessage.c，RFC 2131/2132 的字段）→ AicWifiDataSend（WifiEapolTx.c）
 *         → AicEapolParseData 的解密帧分流（WifiEapolParse.c）→ 本文件声明的 sink。
 * 别改：以太网/IPv4/UDP/DHCP/ARP 的字段偏移由 RFC 定死（每条常量都写了出处）；
 *       AicNetLayoutCheck 在发第一帧之前核对 sizeof/offsetof，排歪了就拒绝发送。
 * 来源：RFC 894 §2（以太网封装）、RFC 791 §3.1（IPv4）、RFC 768（UDP）、
 *       RFC 951 §7（BOOTP 报文最小 300 B）、RFC 2131 §2/§4.1（DHCP 报文、端口 67/68）、
 *       RFC 2132 §3/§9（magic cookie、选项 1/3/6/50/53/54/55/61）、RFC 826（ARP）、
 *       IEEE 802.3 §3.2.7（最小帧 64 B 含 4 B FCS ⇒ 以太网载荷最少 60 B）。
 */
#ifndef SDHCI_WIFI_NET_PRIVATE_H
#define SDHCI_WIFI_NET_PRIVATE_H

#include "WifiHandshakePrivate.h"

/* ---- 以太网（RFC 894 §2；if_ether.h 的 ETH_HLEN/ETH_ALEN/ETH_P_*） ---- */
#define AIC_NET_MAC_LENGTH 6u
#define AIC_NET_ETHERNET_HEADER_LENGTH 14u /* dst[6] + src[6] + ethertype[2] */
#define AIC_NET_ETHERNET_MIN_FRAME 60u     /* 不含 FCS：64（802.3 §3.2.7）- 4（FCS） */
#define AIC_NET_ETHERTYPE_OFFSET 12u
#define AIC_NET_ETHERTYPE_IPV4 0x0800u /* ETH_P_IP（if_ether.h:44） */
#define AIC_NET_ETHERTYPE_ARP 0x0806u  /* ETH_P_ARP（if_ether.h:46） */

/* ---- IPv4 头（RFC 791 §3.1；无选项时 20 B） ---- */
#define AIC_NET_IPV4_HEADER_LENGTH 20u
#define AIC_NET_IPV4_VERSION 4u
#define AIC_NET_IPV4_VERSION_SHIFT 4u
#define AIC_NET_IPV4_IHL_WORDS 5u /* 5 × 4 B = 20 */
#define AIC_NET_IPV4_IHL_MASK 0x0Fu
#define AIC_NET_IPV4_PROTOCOL_UDP 17u /* RFC 791 §3.1 的 protocol；IANA "UDP" */
#define AIC_NET_IPV4_TIME_TO_LIVE 64u /* Linux 默认 64（net/ipv4/ip_output.c） */
#define AIC_NET_IPV4_FLAGS_DF 0x4000u /* RFC 791 §3.1 标志位 bit14（Don't Fragment） */
#define AIC_NET_IPV4_TOTAL_LENGTH_OFFSET 2u
#define AIC_NET_IPV4_PROTOCOL_OFFSET 9u
#define AIC_NET_IPV4_CHECKSUM_OFFSET 10u
#define AIC_NET_IPV4_SOURCE_OFFSET 12u
#define AIC_NET_IPV4_DESTINATION_OFFSET 16u
#define AIC_NET_IPV4_LENGTH 4u /* 一个 IPv4 地址 4 B */
#define AIC_NET_IPV4_TEXT_MAX 16u /* "255.255.255.255" + NUL */

/* ---- UDP 头（RFC 768） ---- */
#define AIC_NET_UDP_HEADER_LENGTH 8u
#define AIC_NET_UDP_SOURCE_PORT_OFFSET 0u
#define AIC_NET_UDP_DESTINATION_PORT_OFFSET 2u
#define AIC_NET_UDP_LENGTH_OFFSET 4u
#define AIC_NET_UDP_CHECKSUM_OFFSET 6u
#define AIC_NET_UDP_CHECKSUM_NONE 0u /* RFC 768：IPv4 的 UDP 校验和 0 = 未计算 */

/* ---- DHCP / BOOTP（RFC 2131 §2 图 1；RFC 2132 §3/§9） ---- */
#define AIC_NET_DHCP_SERVER_PORT 67u /* RFC 2131 §4.1 */
#define AIC_NET_DHCP_CLIENT_PORT 68u
#define AIC_NET_BOOTP_REQUEST 1u /* op */
#define AIC_NET_BOOTP_REPLY 2u
#define AIC_NET_BOOTP_HARDWARE_ETHERNET 1u /* htype */
#define AIC_NET_BOOTP_HARDWARE_LENGTH 6u   /* hlen */
#define AIC_NET_BOOTP_FLAG_BROADCAST 0x8000u /* flags bit0（RFC 2131 §2 图 1） */
#define AIC_NET_DHCP_MAGIC_OFFSET 236u    /* op..file 定长 236 B 之后是 cookie（RFC 2131 §2） */
#define AIC_NET_DHCP_OPTIONS_OFFSET 240u  /* 236 + 4 B cookie */
#define AIC_NET_DHCP_MIN_MESSAGE_LENGTH 300u /* RFC 951 §7：BOOTP 报文至少 300 B */
#define AIC_NET_DHCP_XID_OFFSET 4u
#define AIC_NET_DHCP_OP_OFFSET 0u
#define AIC_NET_DHCP_HARDWARE_TYPE_OFFSET 1u
#define AIC_NET_DHCP_HARDWARE_LENGTH_OFFSET 2u
#define AIC_NET_DHCP_HOPS_OFFSET 3u
#define AIC_NET_DHCP_SECONDS_OFFSET 8u
#define AIC_NET_DHCP_FLAGS_OFFSET 10u
#define AIC_NET_DHCP_YOUR_IP_OFFSET 16u
#define AIC_NET_DHCP_SERVER_IP_OFFSET 20u
#define AIC_NET_DHCP_CLIENT_HARDWARE_OFFSET 28u
#define AIC_NET_DHCP_CLIENT_HARDWARE_LENGTH 16u /* chaddr 字段本身 16 B */
#define AIC_NET_DHCP_MAGIC_COOKIE 0x63825363u /* RFC 2132 §3：99.130.83.99 */
/* 选项号（RFC 2132 §9）与 DHCP 消息类型（§9.6） */
#define AIC_NET_DHCP_OPTION_PAD 0u
#define AIC_NET_DHCP_OPTION_SUBNET_MASK 1u
#define AIC_NET_DHCP_OPTION_ROUTER 3u
#define AIC_NET_DHCP_OPTION_DNS 6u
#define AIC_NET_DHCP_OPTION_REQUESTED_IP 50u
#define AIC_NET_DHCP_OPTION_MESSAGE_TYPE 53u
#define AIC_NET_DHCP_OPTION_SERVER_ID 54u
#define AIC_NET_DHCP_OPTION_PARAMETER_LIST 55u
#define AIC_NET_DHCP_OPTION_CLIENT_ID 61u
#define AIC_NET_DHCP_OPTION_END 255u
#define AIC_NET_DHCP_MESSAGE_DISCOVER 1u
#define AIC_NET_DHCP_MESSAGE_OFFER 2u
#define AIC_NET_DHCP_MESSAGE_REQUEST 3u
#define AIC_NET_DHCP_MESSAGE_ACK 5u
#define AIC_NET_DHCP_MESSAGE_NAK 6u
/* 61 的硬件类型：1 = 以太网（RFC 2132 §9.12），后面跟 6 B MAC */
#define AIC_NET_DHCP_CLIENT_ID_HARDWARE_ETHERNET 1u
/* DISCOVER/REQUEST 的选项最多 30 B（53/61/55/50/54/255），240 + 30 = 270 B，
   离 300 B（RFC 951 §7 的最小 BOOTP 报文）还有 30 B 余量；写超了宁可报错不截断。 */

/* ---- ARP（RFC 826 的包格式） ---- */
#define AIC_NET_ARP_PACKET_LENGTH 28u
#define AIC_NET_ARP_HARDWARE_ETHERNET 1u
#define AIC_NET_ARP_PROTOCOL_IPV4 0x0800u
#define AIC_NET_ARP_HARDWARE_LENGTH 6u
#define AIC_NET_ARP_PROTOCOL_LENGTH 4u
#define AIC_NET_ARP_OPERATION_REQUEST 1u
#define AIC_NET_ARP_OPERATION_REPLY 2u
#define AIC_NET_ARP_HARDWARE_TYPE_OFFSET 0u
#define AIC_NET_ARP_PROTOCOL_TYPE_OFFSET 2u
#define AIC_NET_ARP_HARDWARE_LENGTH_OFFSET 4u
#define AIC_NET_ARP_PROTOCOL_LENGTH_OFFSET 5u
#define AIC_NET_ARP_OPERATION_OFFSET 6u
#define AIC_NET_ARP_SENDER_HARDWARE_OFFSET 8u
#define AIC_NET_ARP_SENDER_PROTOCOL_OFFSET 14u
#define AIC_NET_ARP_TARGET_HARDWARE_OFFSET 18u
#define AIC_NET_ARP_TARGET_PROTOCOL_OFFSET 24u

/* ---- 本刀的帧缓冲与预算 ----
 * 最大的一帧是 DHCP DISCOVER/REQUEST：14 + 20 + 8 + 300 = 342 B；512 与 TX 块同档。
 */
#define AIC_NET_FRAME_MAX 512u
#define AIC_NET_DHCP_FRAME_LENGTH \
    (AIC_NET_ETHERNET_HEADER_LENGTH + AIC_NET_IPV4_HEADER_LENGTH + AIC_NET_UDP_HEADER_LENGTH + \
     AIC_NET_DHCP_MIN_MESSAGE_LENGTH)
/* 默认总预算 8000 ms（现场要求）；上限 30000 ms，再长就不像"一条命令"了 */
#define AIC_NET_DHCP_DEFAULT_MS 8000u
#define AIC_NET_DHCP_MAX_MS 30000u
/*
 * RFC 2131 §4.1 要求客户端重发并退避（4 s、8 s… 带随机）。这里是简化版：
 * DISCOVER 3 次、REQUEST 2 次，每次等"剩余预算 / 剩余次数"，不引入随机定时器。
 */
#define AIC_NET_DHCP_DISCOVER_TRIES 3u
#define AIC_NET_DHCP_REQUEST_TRIES 2u
/* ARP 是"更强的判据"，给它 2 s（两个往返足够；不占 DHCP 的预算） */
#define AIC_NET_ARP_BUDGET_MS 2000u
#define AIC_NET_ARP_TRIES 2u
/* 25 MHz 时间基（BoardConfig.h BOARD_TIMEBASE_HZ），只用来算预算，不参与协议 */
#define AIC_NET_TIMEBASE_PER_MS (BOARD_TIMEBASE_HZ / 1000u)

/* ---- 报文镜像：只用于 AicNetLayoutCheck 的 sizeof/offsetof 核对 ----
 * 字段顺序/宽度照 RFC；UINT16/UINT32 成员只放在自然对齐的位置，编译器不会插填充。
 * 线上字节一律用 AicNetPutBe16/32 按**大端**写，不靠结构体赋值（RISC-V 是小端）。
 */
typedef struct {
    UINT8 Destination[AIC_NET_MAC_LENGTH]; /* +0  */
    UINT8 Source[AIC_NET_MAC_LENGTH];      /* +6  */
    UINT16 EtherType;                      /* +12 */
} AIC_NET_ETHERNET_HEADER;

typedef struct {
    UINT8 VersionAndHeaderLength; /* +0  */
    UINT8 TypeOfService;          /* +1  */
    UINT16 TotalLength;           /* +2  */
    UINT16 Identification;        /* +4  */
    UINT16 FlagsAndFragmentOffset;/* +6  */
    UINT8 TimeToLive;             /* +8  */
    UINT8 Protocol;               /* +9  */
    UINT16 HeaderChecksum;        /* +10 */
    UINT8 Source[4];              /* +12 */
    UINT8 Destination[4];         /* +16 */
} AIC_NET_IPV4_HEADER;

typedef struct {
    UINT16 SourcePort;      /* +0 */
    UINT16 DestinationPort; /* +2 */
    UINT16 Length;          /* +4 */
    UINT16 Checksum;        /* +6 */
} AIC_NET_UDP_HEADER;

typedef struct {
    UINT8 Operation;                    /* +0   */
    UINT8 HardwareType;                 /* +1   */
    UINT8 HardwareLength;               /* +2   */
    UINT8 Hops;                         /* +3   */
    UINT32 TransactionId;               /* +4   */
    UINT16 Seconds;                     /* +8   */
    UINT16 Flags;                       /* +10  */
    UINT8 ClientIp[4];                  /* +12  */
    UINT8 YourIp[4];                    /* +16  */
    UINT8 ServerIp[4];                  /* +20  */
    UINT8 RelayIp[4];                   /* +24  */
    UINT8 ClientHardware[16];           /* +28  */
    UINT8 ServerName[64];               /* +44  */
    UINT8 BootFile[128];                /* +108 */
    UINT32 MagicCookie;                 /* +236 */
} AIC_NET_DHCP_FIXED;

typedef struct {
    UINT16 HardwareType;          /* +0  */
    UINT16 ProtocolType;          /* +2  */
    UINT8 HardwareLength;         /* +4  */
    UINT8 ProtocolLength;         /* +5  */
    UINT16 Operation;             /* +6  */
    UINT8 SenderHardware[6];      /* +8  */
    UINT8 SenderProtocol[4];      /* +14 */
    UINT8 TargetHardware[6];      /* +18 */
    UINT8 TargetProtocol[4];      /* +24 */
    /* sizeof = 28：RFC 826 的报文到 +26 结束，尾部 2 B 是编译器的对齐填充 */
} AIC_NET_ARP_PACKET;

/* 一次 DHCP 应答里本刀真正要的字段（option 53/1/3/6/54 + yiaddr/siaddr） */
typedef struct {
    UINT8 Type;         /* option 53：2 = OFFER、5 = ACK */
    UINT8 YourIp[4];    /* yiaddr（+16） */
    UINT8 ServerIp[4];  /* option 54；没有就退到 siaddr（+20） */
    UINT8 Mask[4];      /* option 1 */
    UINT8 Router[4];    /* option 3（网关） */
    UINT8 Dns[4];       /* option 6（只取第一台） */
    UINT8 HasMask;      /* 0/1 */
    UINT8 HasRouter;    /* 0/1 */
    UINT8 HasDns;       /* 0/1 */
    UINT32 Decryption;  /* hw_rxhdr.status 的 decr：2 = TKIP/GTK、3 = CCMP/PTK */
} AIC_NET_DHCP_REPLY;

/* ---- WifiNetFrame.c：帧公共层 ---- */
/* DHCP/ARP 都要用的固定地址（定义在 WifiNetFrame.c，只读） */
extern const UINT8 gAicNetBroadcastMac[AIC_NET_MAC_LENGTH];
extern const UINT8 gAicNetBroadcastIp[AIC_NET_IPV4_LENGTH];
extern const UINT8 gAicNetZeroIp[AIC_NET_IPV4_LENGTH];
int AicNetLayoutCheck(void);
UINT16 AicNetGetBe16(const UINT8 *At);
UINT32 AicNetGetBe32(const UINT8 *At);
void AicNetPutBe16(UINT8 *At, UINT16 Value);
void AicNetPutBe32(UINT8 *At, UINT32 Value);
UINT16 AicNetChecksum(const UINT8 *Data, UINT32 Length);
UINT8 *AicNetAddEthernetHeader(UINT8 *Frame, const UINT8 *Destination, const UINT8 *Source,
                               UINT16 EtherType);
/*
 * 写 eth + IPv4 + UDP 三层头（42 B），返回 UDP 载荷起点；PayloadLength 决定 IP/UDP 长度
 * 字段与 IP 头校验和。UDP 校验和固定写 0（RFC 768 允许 IPv4 这样做）。
 */
UINT8 *AicNetBuildIpv4UdpFrame(UINT8 *Frame, const UINT8 *Destination, const UINT8 *Source,
                               const UINT8 *SourceIp, const UINT8 *DestinationIp,
                               UINT16 SourcePort, UINT16 DestinationPort, UINT32 PayloadLength);
/* 定位 UDP 载荷：返回载荷起点（0 = 不是 IPv4/UDP、端口不对或长度自相矛盾） */
const UINT8 *AicNetFindUdpPayload(const UINT8 *Payload, UINT32 Length, UINT16 SourcePort,
                                  UINT16 DestinationPort, UINT32 *OutLength);
void AicNetFormatMac(const UINT8 *Address, char *Text);
void AicNetFormatIpv4(const UINT8 *Address, char *Text);
void AicNetMarkMac(const char *Tag, const UINT8 *Address);
/* 本机 STA MAC：优先用 M1 的 addr1（AP 眼里的我们），没有就用 add_if 的 vif MAC */
int AicNetStationAddress(UINT8 *OutAddress);
/* 挂/摘解密数据帧的分流口（DHCP/ARP 各自在 active 时处理） */
void AicNetStart(void);
void AicNetStop(void);
/* 在 BudgetMs 里轮询收包；Done 返回 1 = 条件满足。0 = 满足，-1 = 超时或 SDIO 读失败 */
int AicNetPollRun(SDHCI_HOST *Host, UINT32 BudgetMs, int (*Done)(void));

/* ---- WifiDhcpMessage.c：DHCP 组帧与解析（纯字节，不碰 SDIO） ---- */
UINT32 AicDhcpMakeXid(const UINT8 *Station);
UINT32 AicDhcpBuildDiscover(UINT8 *Frame, UINT32 Capacity, const UINT8 *Station, UINT32 Xid);
UINT32 AicDhcpBuildRequest(UINT8 *Frame, UINT32 Capacity, const UINT8 *Station, UINT32 Xid,
                           const UINT8 *RequestedIp, const UINT8 *ServerIp);
/* 1 = 这条是本 xid 的 DHCP 应答（已填 Reply），0 = 不是/损坏 */
int AicDhcpParseReply(const UINT8 *Payload, UINT32 Length, UINT32 Xid, UINT32 Decryption,
                      AIC_NET_DHCP_REPLY *Reply);

/* ---- WifiDhcp.c：客户端状态机 + `wififw dhcp` 入口 ---- */
void AicDhcpHandlePacket(const UINT8 *Payload, UINT32 Length, UINT32 Decryption);
int SdhciWifiDhcp(const char *MsText);

/* ---- WifiArpMessage.c / WifiArp.c ---- */
UINT32 AicArpBuildRequest(UINT8 *Frame, UINT32 Capacity, const UINT8 *Station,
                          const UINT8 *StationIp, const UINT8 *TargetIp);
/* 1 = 是问 TargetIp 的 ARP reply（已填 OutMac） */
int AicArpParseReply(const UINT8 *Payload, UINT32 Length, const UINT8 *TargetIp, UINT8 *OutMac);
void AicArpHandlePacket(const UINT8 *Payload, UINT32 Length, UINT32 Decryption);
int AicArpResolveUnicast(SDHCI_HOST *Host, const UINT8 *Station, const UINT8 *StationIp,
                         const UINT8 *TargetIp, const UINT8 *DstMac, UINT8 VifIndex,
                         UINT8 ApIndex, UINT8 *OutMac, UINT32 BudgetMs);
int AicArpResolveGateway(SDHCI_HOST *Host, const UINT8 *Station, const UINT8 *StationIp,
                         const UINT8 *GatewayIp, UINT8 VifIndex, UINT8 ApIndex, UINT8 *OutMac,
                         UINT32 BudgetMs);

#endif
