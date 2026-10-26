/*
 * Netif.c — Arm64 lwIP 以太网 netif（rnet-1；对表 RiscV net-6）
 *
 * linkoutput：virt → Virtio NetSendFrame；BOARD_HAS_NET 板 → BoardEthernetSendFrame。
 * RX：驱动在 gLwIpRx 时调 LwIpNetifInput（Pi smsc95xx / virtio）。
 */
#include "lwip/opt.h"
#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include "lwip/netif.h"
#include "lwip/etharp.h"
#include "netif/ethernet.h"
#include "Ip.h"
#include "BoardConfig.h"
#include "Hal.h"
#include "HalDevices.h"
#include "HalConsole.h"
#if BOARD_IS_VIRT
/* Virtio/Net/Private.h */
int NetSendFrame(const UINT8 *Frame, UINTN FrameLen);
#elif BOARD_HAS_NET
#include "Ethernet.h"
#endif

extern void *memcpy(void *Dst, const void *Src, UINTN Len);

static struct netif gLwIpNetif;
static int gLwIpNetifUp;

static const UINT8 gQemuGwMac[6] = { 0x52, 0x55, 0x0a, 0x00, 0x02, 0x02 };
#define TOY_QEMU_GW_IP  0x0A000202U

static void MaybeSeedQemuGwArp(UINT32 Gw) {
    ip4_addr_t GwIp;
    struct eth_addr GwMac;

    if (!HalCpuIsHypervisor() || Gw != TOY_QEMU_GW_IP) {
        return;
    }
    ToyHostIpToLwIp(Gw, &GwIp);
    memcpy(GwMac.addr, gQemuGwMac, 6);
    (void)etharp_add_static_entry(&GwIp, &GwMac);
}

static int PlatformSendEthernet(const UINT8 *Frame, UINTN Length) {
#if BOARD_IS_VIRT
    return NetSendFrame(Frame, Length);
#elif BOARD_HAS_NET
    return BoardEthernetSendFrame(Frame, Length);
#else
    (void)Frame;
    (void)Length;
    return -1;
#endif
}

static err_t LwIpNetifOutput(struct netif *Netif, struct pbuf *P) {
    struct pbuf *Q;
    UINT8 Frame[1518];
    UINTN Off = 0;

    (void)Netif;
    for (Q = P; Q != NULL; Q = Q->next) {
        if (Off + Q->len > sizeof(Frame)) {
            return ERR_BUF;
        }
        memcpy(Frame + Off, Q->payload, Q->len);
        Off += Q->len;
    }
    if (Off < 14) {
        return ERR_BUF;
    }
    return PlatformSendEthernet(Frame, Off) == 0 ? ERR_OK : ERR_IF;
}

static err_t LwIpNetifInit(struct netif *Netif) {
    UINT8 Mac[6];

    HalNetGetMacAddress(Mac);
    Netif->name[0] = 'e';
    Netif->name[1] = 'n';
    Netif->output = etharp_output;
    Netif->linkoutput = LwIpNetifOutput;
    Netif->mtu = 1500;
    Netif->hwaddr_len = ETH_HWADDR_LEN;
    Netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;
    memcpy(Netif->hwaddr, Mac, ETH_HWADDR_LEN);
    return ERR_OK;
}

struct netif *LwIpNetifGet(void) {
    return &gLwIpNetif;
}

int LwIpNetifAdd(UINT32 Ip, UINT32 Mask, UINT32 Gw) {
    ip4_addr_t IpAddr;
    ip4_addr_t NetMask;
    ip4_addr_t GwAddr;

    ToyHostIpToLwIp(Ip, &IpAddr);
    ToyHostIpToLwIp(Mask, &NetMask);
    ToyHostIpToLwIp(Gw, &GwAddr);
    if (netif_add(&gLwIpNetif, &IpAddr, &NetMask, &GwAddr, NULL,
                  LwIpNetifInit, ethernet_input) == NULL) {
        return -1;
    }
    netif_set_default(&gLwIpNetif);
    netif_set_up(&gLwIpNetif);
    gLwIpNetifUp = 1;
    MaybeSeedQemuGwArp(Gw);
    return 0;
}

int LwIpNetifSetAddr(UINT32 Ip, UINT32 Mask, UINT32 Gw) {
    ip4_addr_t IpAddr;
    ip4_addr_t NetMask;
    ip4_addr_t GwAddr;

    if (!gLwIpNetifUp) {
        return -1;
    }
    ToyHostIpToLwIp(Ip, &IpAddr);
    ToyHostIpToLwIp(Mask, &NetMask);
    ToyHostIpToLwIp(Gw, &GwAddr);
    netif_set_addr(&gLwIpNetif, &IpAddr, &NetMask, &GwAddr);
    MaybeSeedQemuGwArp(Gw);
    return 0;
}

void LwIpNetifSyncMac(void) {
    UINT8 Mac[6];
    int i;

    if (!gLwIpNetifUp) {
        return;
    }
    HalNetGetMacAddress(Mac);
    for (i = 0; i < 6; i++) {
        if (gLwIpNetif.hwaddr[i] != Mac[i]) {
            break;
        }
    }
    if (i == 6) {
        return;
    }
    memcpy(gLwIpNetif.hwaddr, Mac, ETH_HWADDR_LEN);
    HalConsoleWriteSerial("netif: mac sync\n");
}

void LwIpNetifInput(const UINT8 *Frame, UINTN Len) {
    struct pbuf *P;

    if (Len < 14 || Len > 1518) {
        return;
    }
    P = pbuf_alloc(PBUF_RAW, (u16_t)Len, PBUF_RAM);
    if (P == NULL) {
        HalConsoleWriteSerial("netif: pbuf_alloc fail\n");
        return;
    }
    if (pbuf_take(P, Frame, (u16_t)Len) != ERR_OK) {
        pbuf_free(P);
        HalConsoleWriteSerial("netif: pbuf_take fail\n");
        return;
    }
    if (gLwIpNetif.input(P, &gLwIpNetif) != ERR_OK) {
        pbuf_free(P);
    }
}
