/*
 * DhcpEpoch.c — NIC epoch → DHCP Restart（从 Dhcp.c 拆出，≤300）
 * Pi WiFi 主出站时吞 SMSC MII bump，勿冲无线租约。
 */
#include "LwIp.h"
#include "NetConfig.h"
#include "Hal.h"
#include "HalDevices.h"
#include "SerialLog.h"

#ifdef LWIP_ENABLE

#include "lwip/dhcp.h"
#include "lwip/netif.h"
#include "Netif.h"

void LwIpDhcpOnNicEpoch(void) {
    static UINT32 sSeen;
    UINT32 Ep;
    struct netif *Netif;

    if (HalCpuIsHypervisor()) {
        return;
    }
    Ep = HalNetNicEpoch();
    if (Ep == 0 || Ep == sSeen) {
        return;
    }
    /*
     * 已有租约：链路/MII 抖一下会再 bump epoch，勿 Restart 刷「dhcp: ok」。
     * 吃掉 epoch，等真正换卡（新 bump 且地址被清）再要。
     */
    Netif = LwIpNetifGet();
    if (LwIpActive() && NetConfigGetIp() != 0 && Netif != 0 &&
        dhcp_supplied_address(Netif)) {
        sSeen = Ep;
        return;
    }
    /*
     * Pi WiFi 主出站：SMSC MII bump 勿 Restart 冲掉无线租约
     * （RUNTIME 曾见探针 dhcp-ok 后 epoch「no offer」把 NetConfig 清掉 → 托盘灰）。
     */
    if (LwIpActive() && NetConfigGetIp() != 0 && HalNetPrimaryKind() == 1) {
        sSeen = Ep;
        return;
    }
    if (LwIpDhcpRestart(12000) == 0) {
        sSeen = Ep;
        LogNet("Net: dhcp queued\n");
    }
}

#else

void LwIpDhcpOnNicEpoch(void) {
}

#endif
