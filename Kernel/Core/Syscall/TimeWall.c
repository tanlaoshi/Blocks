/*
 * TimeWall.c — SYS_TIME_WALL：UTC Unix 秒（自 1970-01-01）
 *
 * HalRtcGetTime 课堂为东八显示（X64：CMOS UTC→CST；Arm64：种子东八）。
 * 先按墙上分量合成秒，再减 8h 得 UTC。无 RTC（如部分 RiscV）→ -1。
 */
#include "SyscallPrivate.h"

#define CLASSROOM_EAST_EIGHT_SECONDS (8 * 3600)

static int IsLeapYear(int Year) {
    return ((Year % 4 == 0 && Year % 100 != 0) || (Year % 400 == 0)) ? 1 : 0;
}

static INT64 CivilDateToUnixSeconds(int Year, int Month, int Day,
                                    int Hour, int Minute, int Second) {
    static const int MonthDays[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    INT64 Days = 0;
    int Y;
    int M;

    if (Year < 1970 || Month < 1 || Month > 12 || Day < 1 || Day > 31 ||
        Hour < 0 || Hour > 23 || Minute < 0 || Minute > 59 ||
        Second < 0 || Second > 60) {
        return -1;
    }
    for (Y = 1970; Y < Year; Y++) {
        Days += IsLeapYear(Y) ? 366 : 365;
    }
    for (M = 1; M < Month; M++) {
        Days += MonthDays[M - 1];
        if (M == 2 && IsLeapYear(Year)) {
            Days += 1;
        }
    }
    Days += Day - 1;
    return Days * 86400LL + (INT64)Hour * 3600LL + (INT64)Minute * 60LL +
           (INT64)Second;
}

INT64 SysTimeWall(void) {
    UINT16 Year;
    UINT8 Month;
    UINT8 Day;
    UINT8 Hour;
    UINT8 Minute;
    UINT8 Second;
    INT64 Seconds;

    if (HalRtcGetTime(&Year, &Month, &Day, &Hour, &Minute, &Second) != 0) {
        return -1;
    }
    Seconds = CivilDateToUnixSeconds((int)Year, (int)Month, (int)Day,
                                     (int)Hour, (int)Minute, (int)Second);
    if (Seconds < 0) {
        return -1;
    }
    return Seconds - (INT64)CLASSROOM_EAST_EIGHT_SECONDS;
}
