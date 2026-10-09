/*
 * NetworkLwip.c — lwIP 初始化 / 轮询 / ping / DNS 门面
 *
 * 【初学者】
 * `lwip on` → lwip_init + NetConfig 绑 netif/DNS + 入站 LwIpNetifInput。
 * 本会话不可逆回 builtin；要回对照栈：重启 QEMU。
 */
#include "LwIp.h"
#include "Network.h"
#include "NetConfig.h"
#include "HalNet.h"
#include "HalSerial.h"
#include "SerialConfig.h"

#if defined(__x86_64__) || defined(_M_X64)
#ifdef HAVE_LWIP

#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "lwip/sys.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "LwIpNetif.h"
#include "LwIpIcmp.h"
#include "LwIpAddr.h"

static int gLwIpReady;
static volatile int gDnsDone;
static volatile err_t gDnsErr;
static ip_addr_t gDnsAddr;

/* freestanding：lwIP 偶调的 libc 符号 */
int memcmp(const void *A, const void *B, unsigned long N) {
    const unsigned char *X = (const unsigned char *)A;
    const unsigned char *Y = (const unsigned char *)B;
    unsigned long i;
    for (i = 0; i < N; i++) {
        if (X[i] != Y[i]) {
            return (int)X[i] - (int)Y[i];
        }
    }
    return 0;
}

void *memmove(void *D, const void *S, unsigned long N) {
    unsigned char *A = (unsigned char *)D;
    const unsigned char *B = (const unsigned char *)S;
    unsigned long i;
    if (A == B || N == 0) {
        return D;
    }
    if (A < B) {
        for (i = 0; i < N; i++) {
            A[i] = B[i];
        }
    } else {
        i = N;
        while (i > 0) {
            i--;
            A[i] = B[i];
        }
    }
    return D;
}

int strncmp(const char *A, const char *B, unsigned long N) {
    unsigned long i;
    for (i = 0; i < N; i++) {
        unsigned char X = (unsigned char)A[i];
        unsigned char Y = (unsigned char)B[i];
        if (X != Y || X == 0) {
            return (int)X - (int)Y;
        }
    }
    return 0;
}

long strtol(const char *S, char **End, int Base) {
    long V = 0;
    int Sign = 1;
    if (S == 0) {
        if (End) {
            *End = 0;
        }
        return 0;
    }
    while (*S == ' ' || *S == '\t') {
        S++;
    }
    if (*S == '-') {
        Sign = -1;
        S++;
    } else if (*S == '+') {
        S++;
    }
    if (Base == 0) {
        Base = 10;
    }
    while (*S) {
        int D;
        if (*S >= '0' && *S <= '9') {
            D = *S - '0';
        } else if (*S >= 'a' && *S <= 'z') {
            D = *S - 'a' + 10;
        } else if (*S >= 'A' && *S <= 'Z') {
            D = *S - 'A' + 10;
        } else {
            break;
        }
        if (D >= Base) {
            break;
        }
        V = V * Base + D;
        S++;
    }
    if (End) {
        *End = (char *)S;
    }
    return V * Sign;
}

u32_t sys_now(void) {
    UINT32 Lo;
    UINT32 Hi;
    UINT64 T;

    __asm__ volatile("rdtsc" : "=a"(Lo), "=d"(Hi));
    T = ((UINT64)Hi << 32) | Lo;
    /* ~3GHz → ms */
    return (u32_t)(T / 3000000ULL);
}

static void PushDns(void) {
    UINT32 DnsHost;
    ip_addr_t DnsServer;
    ip4_addr_t Dns4;

    DnsHost = NetConfigGetDns();
    if (DnsHost == 0) {
        return;
    }
    LwIpNetifSeedSlirp(DnsHost);
    HostIpToLwIp(DnsHost, &Dns4);
    ip_addr_copy_from_ip4(DnsServer, Dns4);
    dns_setserver(0, &DnsServer);
}

int LwIpApplyConfig(void) {
    char Buf[16];

    if (!gLwIpReady) {
        return 0;
    }
    if (LwIpNetifSetAddr(NetConfigGetIp(), NetConfigGetMask(),
                         NetConfigGetGw()) != 0) {
        return -1;
    }
    PushDns();
    NetConfigFormatIp(NetConfigGetDns(), Buf, (int)sizeof(Buf));
    HalSerialWriteShell("lwip: cfg dns=");
    HalSerialWriteShell(Buf);
    HalSerialWriteShell("\n");
    return 0;
}

int LwIpInitialize(void) {
    char Buf[16];

    if (gLwIpReady) {
        return 0;
    }
    if (!HalNetReady()) {
        return -1;
    }
    NetConfigEnsure();
    /* 清空自研单连接，避免与 lwIP 抢 RX */
    NetworkTcpClose();
    NetworkUdpInitialize();
    lwip_init();
    if (LwIpNetifAdd(NetConfigGetIp(), NetConfigGetMask(), NetConfigGetGw()) !=
        0) {
        HalSerialWriteShell("lwip: netif fail\n");
        return -1;
    }
    PushDns();
    gLwIpReady = 1;
    HalSerialWriteShell("lwip: on (RX → lwIP; reboot for builtin)\n");
    NetConfigFormatIp(NetConfigGetDns(), Buf, (int)sizeof(Buf));
    HalSerialWriteShell("lwip: dns=");
    HalSerialWriteShell(Buf);
    HalSerialWriteShell("\n");
    return 0;
}

int LwIpActive(void) {
    return gLwIpReady;
}

void LwIpService(void) {
    UINT8 Rx[1518];
    int RxLen;
    int i;

    if (!gLwIpReady) {
        return;
    }
    for (i = 0; i < 8; i++) {
        RxLen = HalNetReceive(Rx, sizeof(Rx));
        if (RxLen <= 0) {
            break;
        }
        LwIpNetifInput(Rx, (UINTN)RxLen);
    }
    sys_check_timeouts();
}

int LwIpPing(UINT32 DstIp, int TimeoutMs) {
    return LwIpIcmpEcho(DstIp, TimeoutMs);
}

static void DnsFound(const char *Name, const ip_addr_t *Addr, void *Arg) {
    (void)Name;
    (void)Arg;
    if (Addr != NULL) {
        ip_addr_copy(gDnsAddr, *Addr);
        gDnsErr = ERR_OK;
    } else {
        gDnsErr = ERR_VAL;
    }
    gDnsDone = 1;
}

int LwIpDnsLookup(const char *Name, UINT32 *OutIp, int TimeoutMs) {
    err_t Err;
    int Tries;
    ip_addr_t Addr;

    if (Name == 0 || Name[0] == 0 || OutIp == 0) {
        return -1;
    }
    if (NetworkParseIp(Name, OutIp) == 0) {
        return 0;
    }
    if (!gLwIpReady && LwIpInitialize() != 0) {
        return -2;
    }
    gDnsDone = 0;
    gDnsErr = ERR_INPROGRESS;
    ip_addr_set_zero_ip4(&Addr);
    Err = dns_gethostbyname(Name, &Addr, DnsFound, 0);
    if (Err == ERR_OK) {
        *OutIp = LwIpToHostIp(ip_2_ip4(&Addr));
        return 0;
    }
    if (Err != ERR_INPROGRESS) {
        return -3;
    }
    Tries = TimeoutMs > 0 ? TimeoutMs : 5000;
    while (!gDnsDone && Tries-- > 0) {
        LwIpService();
        __asm__ volatile("pause");
    }
    if (!gDnsDone) {
        return -4;
    }
    if (gDnsErr != ERR_OK) {
        return -5;
    }
    *OutIp = LwIpToHostIp(ip_2_ip4(&gDnsAddr));
    return 0;
}

#else /* !HAVE_LWIP */

int LwIpInitialize(void) {
    return -1;
}
int LwIpActive(void) {
    return 0;
}
void LwIpService(void) {
}
int LwIpPing(UINT32 DstIp, int TimeoutMs) {
    (void)DstIp;
    (void)TimeoutMs;
    return -1;
}
int LwIpApplyConfig(void) {
    return 0;
}
int LwIpDnsLookup(const char *Name, UINT32 *OutIp, int TimeoutMs) {
    (void)Name;
    (void)OutIp;
    (void)TimeoutMs;
    return -1;
}

#endif
#else /* !X64 */

int LwIpInitialize(void) {
    return -1;
}
int LwIpActive(void) {
    return 0;
}
void LwIpService(void) {
}
int LwIpPing(UINT32 DstIp, int TimeoutMs) {
    (void)DstIp;
    (void)TimeoutMs;
    return -1;
}
int LwIpApplyConfig(void) {
    return 0;
}
int LwIpDnsLookup(const char *Name, UINT32 *OutIp, int TimeoutMs) {
    (void)Name;
    (void)OutIp;
    (void)TimeoutMs;
    return -1;
}

#endif
