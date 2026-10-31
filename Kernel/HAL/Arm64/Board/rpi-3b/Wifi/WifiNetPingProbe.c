/*
 * WifiNetPingProbe.c — pi-wifi-7e：DHCP 后对网关 LwIpPing
 * LwIpService 拍内已喂 F2；短超时，勿 Present。
 */
#include "Wifi/Private.h"
#ifdef LWIP_ENABLE
#include "Services/LwIp.h"
#include "Services/NetConfig.h"
#include "Netif.h"
#include "lwip/netif.h"
#include "Ip.h"
#endif

int WifiNetPingGateway(UINT32 *OutTargetHostOrder) {
#ifdef LWIP_ENABLE
    UINT32 Target;
    struct netif *Netif;

    Target = NetConfigGetGw();
    if (Target == 0u) {
        Netif = LwIpNetifGet();
        if (Netif == 0) {
            return -1;
        }
        Target = ToyLwIpToHost(netif_ip4_gw(Netif));
    }
    if (OutTargetHostOrder != 0) {
        *OutTargetHostOrder = Target;
    }
    if (Target == 0u) {
        return -1;
    }
    if (LwIpPing(Target, 4000) != 0) {
        return -1;
    }
    return 0;
#else
    (void)OutTargetHostOrder;
    return -1;
#endif
}
