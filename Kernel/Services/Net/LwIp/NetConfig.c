/*
 * NetConfig.c — 静态地址配置（PR-N-nic-addr）
 */
#include "NetConfig.h"
#include "Hal.h"
#include "HalDevices.h"
#include "LwIp.h"
#include "Desktop.h"
#ifdef LWIP_ENABLE
#include "lwip/dns.h"
#include "lwip/netif.h"
#include "Netif.h"
#include "Ip.h"
#endif

#define NET_CFG_IP_QEMU      0x0A00020FU  /* 10.0.2.15 */
#define NET_CFG_MASK_DEFAULT 0xFFFFFF00U  /* /24 */
#define NET_CFG_GW_QEMU      0x0A000202U  /* 10.0.2.2 */
#define NET_CFG_DNS_QEMU     0x0A000203U  /* 10.0.2.3 */

static int gReady;
static UINT32 gIp = NET_CFG_IP_QEMU;
static UINT32 gMask = NET_CFG_MASK_DEFAULT;
static UINT32 gGw;
static UINT32 gDns;

static void ApplyQemuSlirp(void) {
    gIp = NET_CFG_IP_QEMU;
    gGw = NET_CFG_GW_QEMU;
    gDns = NET_CFG_DNS_QEMU;
    HalNetSetIpAddress(gIp);
}

/*
 * QEMU run.sh 用 -cpu qemu64，常不带 Hypervisor CPUID。
 * virtio-net 走胖后端、不挂 NIC_L2；真机 e1000/iwl 会挂 L2。
 */
static int UseQemuSlirp(void) {
    if (HalCpuIsHypervisor()) {
        return 1;
    }
    if (HalNetReady() && !HalNetHasNicL2()) {
        return 1;
    }
    return 0;
}

/*
 * DHCP 可能只写了 lwIP netif（Service 推进），Worker 未 Adopt →
 * NetConfig/gSmscIp 仍 0、托盘红，但 ping 已通（Pi 2026-11-08）。
 * 此处回填配置表与驱动镜像；勿调 HalNetGetIpAddress（防与 Ensure 互递归）。
 */
static UINT32 HealFromLwIpNetif(void) {
#ifdef LWIP_ENABLE
    struct netif *Netif;
    UINT32 Ip;
    UINT32 Mask;
    UINT32 Gateway;
    UINT32 Dns;
    const ip_addr_t *DnsServer;

    if (!LwIpActive()) {
        return 0;
    }
    Netif = LwIpNetifGet();
    if (Netif == 0) {
        return 0;
    }
    Ip = ToyLwIpToHost(netif_ip4_addr(Netif));
    if (Ip == 0u) {
        return 0;
    }
    Mask = ToyLwIpToHost(netif_ip4_netmask(Netif));
    Gateway = ToyLwIpToHost(netif_ip4_gw(Netif));
    Dns = 0;
    DnsServer = dns_getserver(0);
    if (DnsServer != 0 && !ip_addr_isany(DnsServer)) {
        Dns = ToyLwIpToHost(ip_2_ip4(DnsServer));
    }
    if (Mask == 0u) {
        Mask = NET_CFG_MASK_DEFAULT;
    }
    gIp = Ip;
    gMask = Mask;
    gGw = Gateway;
    gDns = Dns;
    HalNetSetIpAddress(Ip);
    DesktopNotifyNetChanged();
    return Ip;
#else
    return 0;
#endif
}

void NetConfigEnsure(void) {
    if (gReady) {
        /*
         * 开机 NetInitialize 若按真机清成 0，virtio 起来后再问 IP 时补回课默认。
         */
        if (gIp == 0 && UseQemuSlirp()) {
            ApplyQemuSlirp();
            return;
        }
        if (gIp == 0u) {
            (void)HealFromLwIpNetif();
            return;
        }
        /*
         * 配置表有址：仅在驱动镜像不一致时推回（勿每拍 SetIp，
         * 托盘/壳 Ensure 很勤）。
         */
        {
            UINT32 DriverIp = HalNetGetIpAddress();

            if (DriverIp != gIp) {
                HalNetSetIpAddress(gIp);
            }
        }
        return;
    }
    if (UseQemuSlirp()) {
        ApplyQemuSlirp();
    } else {
        UINT32 Existing;

        /*
         * 真机不预置课默认址。但勿 HalNetSetIpAddress(0) 清掉驱动已有租约：
         * 托盘 NetConfigGetIp→Ensure 若抢在 Adopt 前，会把壳 `ip` 还能读到的
         * 地址抹掉，且 gIp 一直 0 → 托盘红（Pi 2026-11-04）。
         */
        /*
         * 只读驱动镜像，勿经 NetConfig 回落（Hal 若再调 GetIp→Ensure 会递归）。
         */
        Existing = HalNetGetIpAddress();
        gIp = Existing;
        gGw = 0;
        gDns = 0;
        if (Existing == 0u) {
            /* 驱动空：先试 lwIP 已有租约，再决定是否保持 0 */
            if (HealFromLwIpNetif() == 0u) {
                HalNetSetIpAddress(0);
            }
        }
    }
    gReady = 1;
    if (gIp == 0u && !UseQemuSlirp()) {
        (void)HealFromLwIpNetif();
    }
}

UINT32 NetConfigGetIp(void) {
    NetConfigEnsure();
    return gIp;
}

UINT32 NetConfigGetMask(void) {
    NetConfigEnsure();
    return gMask;
}

UINT32 NetConfigGetGw(void) {
    NetConfigEnsure();
    return gGw;
}

UINT32 NetConfigGetDns(void) {
    NetConfigEnsure();
    return gDns;
}

static int ApplyStack(void) {
    HalNetSetIpAddress(gIp);
    return LwIpApplyConfig();
}

int NetConfigSetIp(UINT32 Ip) {
    int Rc;

    NetConfigEnsure();
    gIp = Ip;
    Rc = ApplyStack();
    if (Rc == 0) {
        DesktopNotifyNetChanged();
    }
    return Rc;
}

int NetConfigSetMask(UINT32 Mask) {
    NetConfigEnsure();
    gMask = Mask;
    return ApplyStack();
}

int NetConfigSetGw(UINT32 Gw) {
    NetConfigEnsure();
    gGw = Gw;
    return ApplyStack();
}

int NetConfigSetDns(UINT32 Dns) {
    NetConfigEnsure();
    gDns = Dns;
    return ApplyStack();
}

void NetConfigAdopt(UINT32 Ip, UINT32 Mask, UINT32 Gw, UINT32 Dns) {
    NetConfigEnsure();
    gIp = Ip;
    gMask = Mask;
    gGw = Gw;
    gDns = Dns;
    HalNetSetIpAddress(Ip);
    /* 对表 X64：配置表变 → 网托随 Worker/Gui 钟栏路径刷新 */
    DesktopNotifyNetChanged();
}
