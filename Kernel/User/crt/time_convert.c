/*
 * time_convert.c — difftime / mktime（展览级命名；按 UTC 日历，无时区库）
 */
#include <time.h>

double difftime(time_t Later, time_t Earlier) {
    return (double)(Later - Earlier);
}

static int YearIsLeap(int Year) {
    return (Year % 4 == 0 && Year % 100 != 0) || (Year % 400 == 0);
}

static int DaysInMonth(int Year, int Month) {
    static const int MonthDays[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };

    if (Month < 0 || Month > 11) {
        return 0;
    }
    if (Month == 1 && YearIsLeap(Year)) {
        return 29;
    }
    return MonthDays[Month];
}

time_t mktime(struct tm *Broken) {
    int Year;
    int Month;
    int Day;
    long Days = 0;
    int WalkYear;
    int WalkMonth;
    time_t Instant;
    struct tm *Normalized;

    if (!Broken) {
        return (time_t)-1;
    }
    Year = Broken->tm_year + 1900;
    Month = Broken->tm_mon;
    Day = Broken->tm_mday;
    if (Year < 1970 || Month < 0 || Month > 11 || Day < 1) {
        return (time_t)-1;
    }
    for (WalkYear = 1970; WalkYear < Year; WalkYear++) {
        Days += YearIsLeap(WalkYear) ? 366 : 365;
    }
    for (WalkMonth = 0; WalkMonth < Month; WalkMonth++) {
        Days += DaysInMonth(Year, WalkMonth);
    }
    Days += Day - 1;
    Instant = (time_t)(Days * 86400L + (long)Broken->tm_hour * 3600L +
                       (long)Broken->tm_min * 60L + (long)Broken->tm_sec);
    Normalized = gmtime(&Instant);
    if (Normalized) {
        *Broken = *Normalized;
    }
    return Instant;
}
