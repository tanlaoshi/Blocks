/*
 * LwIpNetif.c — K41：virtio-net → lwIP netif
 */
#include "lwip/opt.h"
#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include "lwip/netif.h"
#include "lwip/etharp.h"
#include "netif/ethernet.h"
#include "LwIpAddr.h"
#include "LwIpNetif.h"
#include "HalNet.h"
#include "HalSerial.h"
#include "SerialConfig.h"

extern void *memcpy(void *Dst, const void *Src, unsigned long Len);

static struct netif gNetif;
static int gNetifUp;

static const UINT8 gQemuGwMac[6] = { 0x52, 0x55, 0x0a, 0x00, 0x02, 0x02 };
#define QEMU_GW_IP 0x0A000202u

static void SeedGwArp(UINT32 Gw) {
    ip4_addr_t GwIp;
    struct eth_addr GwMac;

    if (Gw != QEMU_GW_IP) {
        return;
    }
    HostIpToLwIp(Gw, &GwIp);
    memcpy(GwMac.addr, gQemuGwMac, 6);
    (void)etharp_add_static_entry(&GwIp, &GwMac);
}

static err_t NetifLinkOutput(struct netif *Netif, struct pbuf *P) {
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
    return HalNetTransmit(Frame, (UINT32)Off) == 0 ? ERR_OK : ERR_IF;
}

static err_t NetifInit(struct netif *Netif) {
    UINT8 Mac[6];

    HalNetGetMac(Mac);
    Netif->name[0] = 'e';
    Netif->name[1] = 'n';
    Netif->output = etharp_output;
    Netif->linkoutput = NetifLinkOutput;
    Netif->mtu = 1500;
    Netif->hwaddr_len = ETH_HWADDR_LEN;
    Netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;
    memcpy(Netif->hwaddr, Mac, ETH_HWADDR_LEN);
    return ERR_OK;
}

int LwIpNetifAdd(UINT32 Ip, UINT32 Mask, UINT32 Gw) {
    ip4_addr_t IpAddr;
    ip4_addr_t NetMask;
    ip4_addr_t GwAddr;

    HostIpToLwIp(Ip, &IpAddr);
    HostIpToLwIp(Mask, &NetMask);
    HostIpToLwIp(Gw, &GwAddr);
    if (netif_add(&gNetif, &IpAddr, &NetMask, &GwAddr, NULL, NetifInit,
                  ethernet_input) == NULL) {
        return -1;
    }
    netif_set_default(&gNetif);
    netif_set_up(&gNetif);
    gNetifUp = 1;
    SeedGwArp(Gw);
    return 0;
}

void LwIpNetifInput(const UINT8 *Frame, UINTN Len) {
    struct pbuf *P;

    if (!gNetifUp || Frame == 0 || Len < 14 || Len > 1518) {
        return;
    }
    P = pbuf_alloc(PBUF_RAW, (u16_t)Len, PBUF_RAM);
    if (P == NULL) {
        HalSerialWriteChannel(SLOG_NET, "netif: pbuf_alloc fail\n");
        return;
    }
    if (pbuf_take(P, Frame, (u16_t)Len) != ERR_OK) {
        pbuf_free(P);
        return;
    }
    if (gNetif.input(P, &gNetif) != ERR_OK) {
        pbuf_free(P);
    }
}
