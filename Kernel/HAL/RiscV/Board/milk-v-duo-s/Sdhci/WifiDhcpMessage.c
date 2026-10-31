/*
 * WifiDhcpMessage.c — PR-36：DHCP DISCOVER/REQUEST 的组帧与 OFFER/ACK 的解析
 *
 * 用户眼里：`wififw dhcp` 发出去的那个 BOOTP 报文里每个字段、以及回来的 OFFER/ACK 里
 *           取出的 yiaddr/mask/gw/dns。
 * 从哪读：AicDhcpBuildDiscover / AicDhcpBuildRequest（→ DhcpBuild 一个函数拼）→
 *         AicDhcpParseReply（→ DhcpReadOption 逐条选项）→ AicDhcpMakeXid（xid 字段）。
 * 别改：字段偏移与选项号来自 RFC 2131 §2 图 1、RFC 2132 §3/§9，本文件不自己发明；
 *       chaddr 是 16 B 定长字段（前 6 B = MAC，其余补 0），flags 置 0x8000 要求广播回包。
 * 本刀不做：租约续期（RENEW/REBIND）、RELEASE、NAK 处理 —— 只要一个能用的地址。
 * 校验和：IPv4 头由 WifiNetFrame.c 算；UDP 校验和写 0（RFC 768 对 IPv4 允许）。
 */
#include "WifiNetPrivate.h"
#include "CString.h"

/* 写一条 DHCP 选项（RFC 2132 §2 的 code|len|value 三元组），返回写入字节数 */
static UINT32 DhcpAddOption(UINT8 *Body, UINT32 Offset, UINT8 Code, const UINT8 *Value,
                            UINT32 Length) {
    Body[Offset] = Code;
    Body[Offset + 1u] = (UINT8)Length;
    (void)memcpy(Body + Offset + 2u, Value, Length);
    return Length + 2u;
}

/*
 * 选项 53：DHCP Message Type（RFC 2132 §9.6）。
 * 选项 61：Client-identifier = htype(1) + chaddr（RFC 2132 §9.12），以太网 htype=1。
 * 选项 55：Parameter Request List = 1,3,6（子网掩码/网关/DNS，现场要求）。
 * 返回写完之后的绝对偏移（调用方接着写 50/54/255）。
 */
static UINT32 DhcpAddStandardOptions(UINT8 *Body, UINT32 Offset, UINT8 Type,
                                     const UINT8 *Station) {
    UINT8 ClientId[AIC_NET_MAC_LENGTH + 1u];
    static const UINT8 ParameterList[3] = {
        AIC_NET_DHCP_OPTION_SUBNET_MASK, AIC_NET_DHCP_OPTION_ROUTER, AIC_NET_DHCP_OPTION_DNS};
    UINT32 Index;

    ClientId[0] = AIC_NET_DHCP_CLIENT_ID_HARDWARE_ETHERNET;
    for (Index = 0; Index < AIC_NET_MAC_LENGTH; Index++) {
        ClientId[Index + 1u] = Station[Index];
    }
    Offset += DhcpAddOption(Body, Offset, AIC_NET_DHCP_OPTION_MESSAGE_TYPE, &Type, 1u);
    Offset += DhcpAddOption(Body, Offset, AIC_NET_DHCP_OPTION_CLIENT_ID, ClientId,
                            (UINT32)sizeof(ClientId));
    Offset += DhcpAddOption(Body, Offset, AIC_NET_DHCP_OPTION_PARAMETER_LIST, ParameterList,
                            (UINT32)sizeof(ParameterList));
    return Offset;
}

/*
 * 拼一整帧：以太网 + IPv4 + UDP + BOOTP，返回帧长（含 14 B 以太网头）。
 * 报文固定 300 B（RFC 951 §7 的最小 BOOTP 报文），短选项后面全是 0 补齐，
 * 所以整帧长恒为 14+20+8+300 = 342 B —— 也天然满足 60 B 的以太网最小帧。
 */
static UINT32 DhcpBuild(UINT8 *Frame, UINT32 Capacity, const UINT8 *Station, UINT32 Xid,
                        UINT8 Type, const UINT8 *RequestedIp, const UINT8 *ServerIp) {
    UINT8 *Body;
    UINT32 Offset;

    if (Frame == 0 || Station == 0 || Capacity < AIC_NET_DHCP_FRAME_LENGTH) {
        return 0u;
    }
    Body = AicNetBuildIpv4UdpFrame(Frame, gAicNetBroadcastMac, Station, gAicNetZeroIp,
                                   gAicNetBroadcastIp, AIC_NET_DHCP_CLIENT_PORT,
                                   AIC_NET_DHCP_SERVER_PORT, AIC_NET_DHCP_MIN_MESSAGE_LENGTH);
    Body[AIC_NET_DHCP_OP_OFFSET] = AIC_NET_BOOTP_REQUEST;
    Body[AIC_NET_DHCP_HARDWARE_TYPE_OFFSET] = AIC_NET_BOOTP_HARDWARE_ETHERNET;
    Body[AIC_NET_DHCP_HARDWARE_LENGTH_OFFSET] = AIC_NET_BOOTP_HARDWARE_LENGTH;
    Body[AIC_NET_DHCP_HOPS_OFFSET] = 0u;
    AicNetPutBe32(Body + AIC_NET_DHCP_XID_OFFSET, Xid);
    AicNetPutBe16(Body + AIC_NET_DHCP_SECONDS_OFFSET, 0u); /* seconds：不记已用时间 */
    AicNetPutBe16(Body + AIC_NET_DHCP_FLAGS_OFFSET, AIC_NET_BOOTP_FLAG_BROADCAST);
    (void)memcpy(Body + AIC_NET_DHCP_CLIENT_HARDWARE_OFFSET, Station, AIC_NET_MAC_LENGTH);
    AicNetPutBe32(Body + AIC_NET_DHCP_MAGIC_OFFSET, AIC_NET_DHCP_MAGIC_COOKIE);
    Offset = AIC_NET_DHCP_OPTIONS_OFFSET;
    Offset = DhcpAddStandardOptions(Body, Offset, Type, Station);
    if (RequestedIp != 0) {
        Offset += DhcpAddOption(Body, Offset, AIC_NET_DHCP_OPTION_REQUESTED_IP, RequestedIp,
                                AIC_NET_IPV4_LENGTH);
    }
    if (ServerIp != 0) {
        Offset += DhcpAddOption(Body, Offset, AIC_NET_DHCP_OPTION_SERVER_ID, ServerIp,
                                AIC_NET_IPV4_LENGTH);
    }
    if (Offset + 1u > AIC_NET_DHCP_MIN_MESSAGE_LENGTH) {
        return 0u; /* 选项写过了 300 B：宁可报错也不截断（截断的报文服务器会丢） */
    }
    Body[Offset] = AIC_NET_DHCP_OPTION_END;
    return AIC_NET_DHCP_FRAME_LENGTH;
}

UINT32 AicDhcpBuildDiscover(UINT8 *Frame, UINT32 Capacity, const UINT8 *Station, UINT32 Xid) {
    return DhcpBuild(Frame, Capacity, Station, Xid, AIC_NET_DHCP_MESSAGE_DISCOVER, 0, 0);
}

UINT32 AicDhcpBuildRequest(UINT8 *Frame, UINT32 Capacity, const UINT8 *Station, UINT32 Xid,
                           const UINT8 *RequestedIp, const UINT8 *ServerIp) {
    return DhcpBuild(Frame, Capacity, Station, Xid, AIC_NET_DHCP_MESSAGE_REQUEST, RequestedIp,
                     ServerIp);
}

/*
 * xid 字段（RFC 2131 §4.1）：客户端自己选，用来把应答对回本事务。两次 rdtime 加本机 MAC
 * 混一下即可（不追求密码学随机，只求同一台机器每次不同、0 不用）。
 */
UINT32 AicDhcpMakeXid(const UINT8 *Station) {
    UINT32 Xid = (UINT32)(SdhciReadTime() & 0xFFFFFFFFu);

    Xid ^= (UINT32)(SdhciReadTime() >> 13);
    Xid ^= ((UINT32)Station[2] << 16) | ((UINT32)Station[3] << 8) | (UINT32)Station[4];
    return (Xid == 0u) ? 1u : Xid;
}

/* 一条选项落到结果结构里；不认识的选项直接跳过（RFC 2132 §2：客户端必须忽略） */
static void DhcpReadOption(AIC_NET_DHCP_REPLY *Reply, UINT8 Code, const UINT8 *Value,
                           UINT32 Length) {
    if (Length < AIC_NET_IPV4_LENGTH) {
        return;
    }
    if (Code == AIC_NET_DHCP_OPTION_SUBNET_MASK) {
        (void)memcpy(Reply->Mask, Value, AIC_NET_IPV4_LENGTH);
        Reply->HasMask = 1u;
    } else if (Code == AIC_NET_DHCP_OPTION_ROUTER) {
        (void)memcpy(Reply->Router, Value, AIC_NET_IPV4_LENGTH);
        Reply->HasRouter = 1u;
    } else if (Code == AIC_NET_DHCP_OPTION_DNS) {
        (void)memcpy(Reply->Dns, Value, AIC_NET_IPV4_LENGTH); /* 只取第一台 */
        Reply->HasDns = 1u;
    } else if (Code == AIC_NET_DHCP_OPTION_SERVER_ID) {
        (void)memcpy(Reply->ServerIp, Value, AIC_NET_IPV4_LENGTH);
    }
}

/*
 * 解析一条服务器应答。xid 是"这条是不是回给我们的"的唯一判据（RFC 2131 §4.1：
 * 客户端必须丢弃 xid 不匹配的报文）；chaddr 不做硬校验 —— 万一固件空口用的 MAC
 * 与我们记的不同，卡在这里就完全看不到 OFFER，宁可放行、让 xid 挡别人的包。
 */
int AicDhcpParseReply(const UINT8 *Payload, UINT32 Length, UINT32 Xid, UINT32 Decryption,
                      AIC_NET_DHCP_REPLY *Reply) {
    AIC_NET_DHCP_REPLY Parsed;
    const UINT8 *Body;
    UINT32 BodyLength = 0;
    UINT32 Index;

    if (Reply == 0) {
        return 0;
    }
    Body = AicNetFindUdpPayload(Payload, Length, AIC_NET_DHCP_SERVER_PORT,
                                AIC_NET_DHCP_CLIENT_PORT, &BodyLength);
    if (Body == 0 || BodyLength < AIC_NET_DHCP_OPTIONS_OFFSET) {
        return 0;
    }
    if (Body[AIC_NET_DHCP_OP_OFFSET] != AIC_NET_BOOTP_REPLY) {
        return 0;
    }
    if (AicNetGetBe32(Body + AIC_NET_DHCP_XID_OFFSET) != Xid) {
        return 0;
    }
    if (AicNetGetBe32(Body + AIC_NET_DHCP_MAGIC_OFFSET) != AIC_NET_DHCP_MAGIC_COOKIE) {
        return 0;
    }
    memset(&Parsed, 0, sizeof(Parsed));
    (void)memcpy(Parsed.YourIp, Body + AIC_NET_DHCP_YOUR_IP_OFFSET, AIC_NET_IPV4_LENGTH);
    (void)memcpy(Parsed.ServerIp, Body + AIC_NET_DHCP_SERVER_IP_OFFSET, AIC_NET_IPV4_LENGTH);
    Parsed.Decryption = Decryption;
    Index = AIC_NET_DHCP_OPTIONS_OFFSET;
    while (Index < BodyLength) {
        UINT8 Code = Body[Index];
        UINT32 OptionLength;

        if (Code == AIC_NET_DHCP_OPTION_PAD) {
            Index++;
            continue;
        }
        if (Code == AIC_NET_DHCP_OPTION_END) {
            break;
        }
        if (Index + 2u > BodyLength) {
            break;
        }
        OptionLength = (UINT32)Body[Index + 1u];
        if (OptionLength > BodyLength - Index - 2u) {
            break; /* 选项长度越过报文尾：整条丢弃，不做半截解析 */
        }
        if (Code == AIC_NET_DHCP_OPTION_MESSAGE_TYPE && OptionLength == 1u) {
            Parsed.Type = Body[Index + 2u];
        } else {
            DhcpReadOption(&Parsed, Code, Body + Index + 2u, OptionLength);
        }
        Index += 2u + OptionLength;
    }
    if (Parsed.Type == 0u) {
        return 0; /* 没有 option 53：不是 DHCP 应答（BOOTP 老式服务器） */
    }
    *Reply = Parsed;
    return 1;
}
