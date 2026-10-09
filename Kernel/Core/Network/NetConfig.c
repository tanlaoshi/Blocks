/*
 * NetConfig.c — K42：静态地址表（对标现网 NetConfig 薄）
 *
 * 【初学者】X64 QEMU 课路径固定 SLIRP 地址；真机/DHCP 另刀。
 */
#include "NetConfig.h"
#include "LwIp.h"

#define NET_CFG_IP_QEMU  0x0A00020Fu /* 10.0.2.15 */
#define NET_CFG_MASK     0xFFFFFF00u /* /24 */
#define NET_CFG_GW_QEMU  0x0A000202u /* 10.0.2.2 */
#define NET_CFG_DNS_QEMU 0x0A000203u /* 10.0.2.3 */

static int gReady;
static UINT32 gIp = NET_CFG_IP_QEMU;
static UINT32 gMask = NET_CFG_MASK;
static UINT32 gGw = NET_CFG_GW_QEMU;
static UINT32 gDns = NET_CFG_DNS_QEMU;

void NetConfigEnsure(void) {
    if (gReady) {
        return;
    }
    gIp = NET_CFG_IP_QEMU;
    gMask = NET_CFG_MASK;
    gGw = NET_CFG_GW_QEMU;
    gDns = NET_CFG_DNS_QEMU;
    gReady = 1;
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

static int Apply(void) {
    return LwIpApplyConfig();
}

int NetConfigSetIp(UINT32 Ip) {
    NetConfigEnsure();
    gIp = Ip;
    return Apply();
}

int NetConfigSetMask(UINT32 Mask) {
    NetConfigEnsure();
    gMask = Mask;
    return Apply();
}

int NetConfigSetGw(UINT32 Gw) {
    NetConfigEnsure();
    gGw = Gw;
    return Apply();
}

int NetConfigSetDns(UINT32 Dns) {
    NetConfigEnsure();
    gDns = Dns;
    return Apply();
}

void NetConfigFormatIp(UINT32 Ip, char *Buf, int Cap) {
    int N = 0;
    int Oct;
    int i;
    UINT8 O[4];

    if (Buf == 0 || Cap < 8) {
        return;
    }
    O[0] = (UINT8)((Ip >> 24) & 0xFFu);
    O[1] = (UINT8)((Ip >> 16) & 0xFFu);
    O[2] = (UINT8)((Ip >> 8) & 0xFFu);
    O[3] = (UINT8)(Ip & 0xFFu);
    for (Oct = 0; Oct < 4; Oct++) {
        char Tmp[4];
        int Dig = 0;
        UINT8 V = O[Oct];
        if (V >= 100u) {
            Tmp[Dig++] = (char)('0' + V / 100u);
            V = (UINT8)(V % 100u);
            Tmp[Dig++] = (char)('0' + V / 10u);
            Tmp[Dig++] = (char)('0' + V % 10u);
        } else if (V >= 10u) {
            Tmp[Dig++] = (char)('0' + V / 10u);
            Tmp[Dig++] = (char)('0' + V % 10u);
        } else {
            Tmp[Dig++] = (char)('0' + V);
        }
        for (i = 0; i < Dig && N + 1 < Cap; i++) {
            Buf[N++] = Tmp[i];
        }
        if (Oct < 3 && N + 1 < Cap) {
            Buf[N++] = '.';
        }
    }
    Buf[N] = 0;
}
