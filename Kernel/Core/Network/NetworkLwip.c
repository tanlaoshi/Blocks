/*
 * NetworkLwip.c — K41：lwIP 初始化 / 轮询 / ping 门面
 *
 * 【初学者】
 * `lwip on` → lwip_init + netif(10.0.2.15) + 入站改走 LwIpNetifInput。
 * 本会话不可逆回 builtin（对标现网）；要回对照栈：重启 QEMU。
 */
#include "LwIp.h"
#include "Network.h"
#include "HalNet.h"
#include "HalSerial.h"
#include "SerialConfig.h"

#if defined(__x86_64__) || defined(_M_X64)
#ifdef HAVE_LWIP

#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "lwip/sys.h"
#include "LwIpNetif.h"
#include "LwIpIcmp.h"

static int gLwIpReady;

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

int LwIpInitialize(void) {
    if (gLwIpReady) {
        return 0;
    }
    if (!HalNetReady()) {
        return -1;
    }
    /* 清空自研单连接，避免与 lwIP 抢 RX */
    NetworkTcpClose();
    NetworkUdpInitialize();
    lwip_init();
    if (LwIpNetifAdd(NetworkSelfIp(), 0xFFFFFF00u, NetworkGwIp()) != 0) {
        HalSerialWriteShell("lwip: netif fail\n");
        return -1;
    }
    gLwIpReady = 1;
    HalSerialWriteShell("lwip: on (RX → lwIP; reboot for builtin)\n");
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

#endif
