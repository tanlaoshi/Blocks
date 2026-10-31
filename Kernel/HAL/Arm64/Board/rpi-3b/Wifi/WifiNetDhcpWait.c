/*
 * WifiNetDhcpWait.c — pi-wifi-7d：Kick 后等租约（LwIpDhcpStep + F2 喂）
 * 一次一变量：持续喂 RX + 站点 MAC；落盘 IP 或失败。
 */
#include "Wifi/Private.h"
#ifdef LWIP_ENABLE
#include "Services/LwIp.h"
#include "Netif.h"
#include "lwip/dhcp.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "Ip.h"
#endif

#define WIFI_NET_DHCP_WAIT_ATTEMPTS 12000u

int WifiNetWaitDhcpLease(UINT32 *OutIpHostOrder) {
#ifdef LWIP_ENABLE
    UINT32 Attempt;
    struct netif *Netif;

    if (OutIpHostOrder != 0) {
        *OutIpHostOrder = 0;
    }
    Netif = LwIpNetifGet();
    if (Netif == 0) {
        return -1;
    }
    for (Attempt = 0; Attempt < WIFI_NET_DHCP_WAIT_ATTEMPTS; Attempt++) {
        int StepDone;

        if ((Attempt & 0xFFu) == 0u) {
            (void)WifiSdioSleepControlEnsureAwake();
        }
        StepDone = LwIpDhcpStep();
        if (dhcp_supplied_address(Netif)) {
            if (OutIpHostOrder != 0) {
                *OutIpHostOrder = ToyLwIpToHost(netif_ip4_addr(Netif));
            }
            return 0;
        }
        if (StepDone != 0) {
            return -1;
        }
        SdhciDelayMicroseconds(1000);
    }
    return -1;
#else
    (void)OutIpHostOrder;
    return -1;
#endif
}
