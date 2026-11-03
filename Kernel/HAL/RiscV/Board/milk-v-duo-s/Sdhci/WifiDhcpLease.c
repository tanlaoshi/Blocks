/*
 * WifiDhcpLease.c — 记住最近一次 DHCP ACK；并推进通用 NetConfig/lwIP（双栈）
 *
 * dhcp 成功后写入；reboot / 未 dhcp 前 Get 失败。不落盘。
 * 2026-11-03：ACK 后 HalNetWifiAdoptLease —— `ip` 改 via wifi，ksend serve 才能走热点。
 */
#include "WifiNetPrivate.h"
#include "HalDevices.h"

static AIC_NET_DHCP_REPLY sAicDhcpLease;
static UINT32 sAicDhcpLeaseValid;

static UINT32 LeaseIpv4Host(const UINT8 Bytes[4]) {
    return ((UINT32)Bytes[0] << 24) | ((UINT32)Bytes[1] << 16) |
           ((UINT32)Bytes[2] << 8) | (UINT32)Bytes[3];
}

void AicDhcpLeaseStore(const AIC_NET_DHCP_REPLY *Reply) {
    UINT32 Ip;
    UINT32 Mask;
    UINT32 Gateway;
    UINT32 Dns;

    if (Reply == 0 || Reply->Type != AIC_NET_DHCP_MESSAGE_ACK) {
        sAicDhcpLeaseValid = 0u;
        return;
    }
    sAicDhcpLease = *Reply;
    sAicDhcpLeaseValid = 1u;

    Ip = LeaseIpv4Host(Reply->YourIp);
    Mask = Reply->HasMask ? LeaseIpv4Host(Reply->Mask) : 0u;
    Gateway = Reply->HasRouter ? LeaseIpv4Host(Reply->Router) : 0u;
    Dns = Reply->HasDns ? LeaseIpv4Host(Reply->Dns) : Gateway;
    HalNetWifiAdoptLease(Ip, Mask, Gateway, Dns);
}

int AicDhcpLeaseGet(AIC_NET_DHCP_REPLY *OutReply) {
    if (OutReply == 0 || sAicDhcpLeaseValid == 0u) {
        return -1;
    }
    *OutReply = sAicDhcpLease;
    return 0;
}
