/*
 * Platform.c — Arm64 平台 MMIO 占位 + 软墙钟
 *
 * Pi3B+ 无板载电池 RTC；mailbox GET_CLOCK_RATE 只报 Hz，不是墙钟。
 * 用 CNTVCT/CNTFRQ（ARM 体系计时器）从编译期种子推进；任务栏已接 HalRtcGetTime。
 * 种子按课堂本地（东八）写，勿再套 X64 的 UTC→CST。
 */
#include "Hal.h"
#include "HalSerial.h"

static int gRtcInited;
static UINT64 gRtcBaseCnt;
static UINT64 gRtcFreq;
static UINT16 gSeedYear;
static UINT8 gSeedMonth;
static UINT8 gSeedDay;
static UINT8 gSeedHour;
static UINT8 gSeedMinute;
static UINT8 gSeedSecond;

void HalPlatformMapMmio(void) {
}

UINT64 HalPlatformXhciFallback(void) {
    return 0;
}

void HalPlatformSetSystemTable(void *SystemTable) {
    (void)SystemTable;
}

void *HalPlatformSystemTable(void) {
    return 0;
}

void HalPlatformNoteRuntimeRange(UINT64 Phys, UINT64 Size) {
    (void)Phys;
    (void)Size;
}

int HalPlatformRuntimeRangeCount(void) {
    return 0;
}

void HalVirtPlatformIdleLoop(void) {
    for (;;) {
        HalCpuPark();
    }
}

static int SoftLeap(UINT16 Year) {
    if ((Year % 4u) != 0) {
        return 0;
    }
    if ((Year % 100u) != 0) {
        return 1;
    }
    return ((Year % 400u) == 0) ? 1 : 0;
}

static UINT8 SoftDaysInMonth(UINT16 Year, UINT8 Month) {
    static const UINT8 Days[13] = {
        0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };

    if (Month < 1 || Month > 12) {
        return 30;
    }
    if (Month == 2 && SoftLeap(Year)) {
        return 29;
    }
    return Days[Month];
}

static void SoftAddSeconds(UINT16 *Year, UINT8 *Month, UINT8 *Day,
                           UINT8 *Hour, UINT8 *Minute, UINT8 *Second,
                           UINT64 Add) {
    UINT64 Sec;
    UINT64 Days;
    UINT8 Dim;

    Sec = (UINT64)(*Second) + Add;
    *Second = (UINT8)(Sec % 60u);
    Sec /= 60u;
    Sec += (UINT64)(*Minute);
    *Minute = (UINT8)(Sec % 60u);
    Sec /= 60u;
    Sec += (UINT64)(*Hour);
    *Hour = (UINT8)(Sec % 24u);
    Days = Sec / 24u;
    while (Days > 0) {
        Dim = SoftDaysInMonth(*Year, *Month);
        if ((UINT64)(*Day) + Days <= (UINT64)Dim) {
            *Day = (UINT8)((UINT64)(*Day) + Days);
            break;
        }
        Days -= (UINT64)(Dim - *Day + 1u);
        *Day = 1;
        (*Month)++;
        if (*Month > 12) {
            *Month = 1;
            (*Year)++;
        }
    }
}

static int SoftMonth(const char *Mon3) {
    static const char *Names = "JanFebMarAprMayJunJulAugSepOctNovDec";
    int i;
    for (i = 0; i < 12; i++) {
        if (Mon3[0] == Names[i * 3] && Mon3[1] == Names[i * 3 + 1] &&
            Mon3[2] == Names[i * 3 + 2]) {
            return i + 1;
        }
    }
    return 10;
}

static void SoftRtcEnsure(void) {
    const char *D = __DATE__; /* "Mmm dd yyyy" */
    const char *T = __TIME__; /* "hh:mm:ss" */
    int Mo;

    if (gRtcInited) {
        return;
    }
    /*
     * 无板载 RTC/NTP：种子 = 编译时刻（本地），CNTV 推进。
     * 编完即 CK 则接近墙上钟；久置会漂。Settings/NTP 另刀。
     */
    Mo = SoftMonth(D);
    gSeedYear = (UINT16)((D[7] - '0') * 1000 + (D[8] - '0') * 100 +
                         (D[9] - '0') * 10 + (D[10] - '0'));
    gSeedMonth = (UINT8)Mo;
    gSeedDay = (UINT8)((D[4] == ' ' ? 0 : (D[4] - '0')) * 10 + (D[5] - '0'));
    gSeedHour = (UINT8)((T[0] - '0') * 10 + (T[1] - '0'));
    gSeedMinute = (UINT8)((T[3] - '0') * 10 + (T[4] - '0'));
    gSeedSecond = (UINT8)((T[6] - '0') * 10 + (T[7] - '0'));
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(gRtcFreq));
    if (gRtcFreq == 0) {
        gRtcFreq = 19200000ULL; /* BCM2837 常见 19.2MHz */
    }
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(gRtcBaseCnt));
    gRtcInited = 1;
    HalSerialBootMark("Boot: RTC soft CNTV seed=__DATE__/__TIME__\n");
}

int HalRtcGetTime(UINT16 *Year, UINT8 *Month, UINT8 *Day,
                  UINT8 *Hour, UINT8 *Minute, UINT8 *Second) {
    UINT64 Now;
    UINT64 Elapsed;
    UINT16 Y;
    UINT8 Mo, D, H, Mi, S;

    SoftRtcEnsure();
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(Now));
    Elapsed = (Now - gRtcBaseCnt) / gRtcFreq;
    Y = gSeedYear;
    Mo = gSeedMonth;
    D = gSeedDay;
    H = gSeedHour;
    Mi = gSeedMinute;
    S = gSeedSecond;
    SoftAddSeconds(&Y, &Mo, &D, &H, &Mi, &S, Elapsed);
    if (Year) {
        *Year = Y;
    }
    if (Month) {
        *Month = Mo;
    }
    if (Day) {
        *Day = D;
    }
    if (Hour) {
        *Hour = H;
    }
    if (Minute) {
        *Minute = Mi;
    }
    if (Second) {
        *Second = S;
    }
    return 0;
}
