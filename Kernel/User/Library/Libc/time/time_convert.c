/*
 * time_convert.c — difftime / mktime（全称下划线；按 UTC 日历，无时区库）
 */
#include <time.h>

double difftime(time_t later, time_t earlier) {
    return (double)(later - earlier);
}

static int year_is_leap(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static int days_in_month(int year, int month) {
    static const int month_days[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };

    if (month < 0 || month > 11) {
        return 0;
    }
    if (month == 1 && year_is_leap(year)) {
        return 29;
    }
    return month_days[month];
}

time_t mktime(struct tm *broken) {
    int year;
    int month;
    int day;
    long days = 0;
    int walk_year;
    int walk_month;
    time_t instant;
    struct tm *normalized;

    if (!broken) {
        return (time_t)-1;
    }
    year = broken->tm_year + 1900;
    month = broken->tm_mon;
    day = broken->tm_mday;
    if (year < 1970 || month < 0 || month > 11 || day < 1) {
        return (time_t)-1;
    }
    for (walk_year = 1970; walk_year < year; walk_year++) {
        days += year_is_leap(walk_year) ? 366 : 365;
    }
    for (walk_month = 0; walk_month < month; walk_month++) {
        days += days_in_month(year, walk_month);
    }
    days += day - 1;
    instant = (time_t)(days * 86400L + (long)broken->tm_hour * 3600L +
                       (long)broken->tm_min * 60L + (long)broken->tm_sec);
    normalized = gmtime(&instant);
    if (normalized) {
        *broken = *normalized;
    }
    return instant;
}
