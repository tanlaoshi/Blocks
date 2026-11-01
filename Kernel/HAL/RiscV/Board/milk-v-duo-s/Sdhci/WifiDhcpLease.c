/*
 * WifiDhcpLease.c — 记住最近一次 DHCP ACK 租约，供 `wififw ping` 用
 *
 * dhcp 成功后写入；reboot / 未 dhcp 前 Get 失败。不落盘（对表 Pi 壳级 ping 即可）。
 */
#include "WifiNetPrivate.h"
#include "CString.h"

static AIC_NET_DHCP_REPLY sAicDhcpLease;
static UINT32 sAicDhcpLeaseValid;

void AicDhcpLeaseStore(const AIC_NET_DHCP_REPLY *Reply) {
    if (Reply == 0 || Reply->Type != AIC_NET_DHCP_MESSAGE_ACK) {
        sAicDhcpLeaseValid = 0u;
        return;
    }
    sAicDhcpLease = *Reply;
    sAicDhcpLeaseValid = 1u;
}

int AicDhcpLeaseGet(AIC_NET_DHCP_REPLY *OutReply) {
    if (OutReply == 0 || sAicDhcpLeaseValid == 0u) {
        return -1;
    }
    *OutReply = sAicDhcpLease;
    return 0;
}
