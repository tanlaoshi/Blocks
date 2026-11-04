/*
 * time.c — clock / time / 最小 gmtime/asctime/strftime（展览级命名）
 *
 * time()：SYS_TIME_WALL → UTC Unix 秒；失败 (time_t)-1（C99）。
 * clock()：仍用单调 SYS_CLOCK_MS。
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <Blocks/syscall.h>

clock_t clock(void) {
    return (clock_t)clock_ms();
}

time_t time(time_t *Out) {
    long WallSeconds = blocks_time_wall();
    time_t Now;

    if (WallSeconds < 0) {
        return (time_t)-1;
    }
    Now = (time_t)WallSeconds;
    if (Out) {
        *Out = Now;
    }
    return Now;
}

static struct tm BrokenTime;
static char AsctimeBuffer[32];

struct tm *gmtime(const time_t *Timer) {
    time_t Instant;
    long Days;
    long SecondsOfDay;
    int Year;
    int YearLength;
    static const int MonthDays[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    int Month;
    int LeapYear;
    int DaysInMonth;

    if (!Timer) {
        return 0;
    }
    Instant = *Timer;
    if (Instant < 0) {
        Instant = 0;
    }
    Days = Instant / 86400L;
    SecondsOfDay = Instant % 86400L;
    BrokenTime.tm_hour = (int)(SecondsOfDay / 3600L);
    BrokenTime.tm_min = (int)((SecondsOfDay % 3600L) / 60L);
    BrokenTime.tm_sec = (int)(SecondsOfDay % 60L);
    BrokenTime.tm_wday = (int)((Days + 4) % 7); /* 1970-01-01 Thursday */
    Year = 1970;
    for (;;) {
        LeapYear =
            ((Year % 4 == 0 && Year % 100 != 0) || (Year % 400 == 0));
        YearLength = LeapYear ? 366 : 365;
        if (Days < YearLength) {
            break;
        }
        Days -= YearLength;
        Year++;
    }
    BrokenTime.tm_year = Year - 1900;
    BrokenTime.tm_yday = (int)Days;
    LeapYear = ((Year % 4 == 0 && Year % 100 != 0) || (Year % 400 == 0));
    for (Month = 0; Month < 12; Month++) {
        DaysInMonth = MonthDays[Month];
        if (Month == 1 && LeapYear) {
            DaysInMonth = 29;
        }
        if (Days < DaysInMonth) {
            break;
        }
        Days -= DaysInMonth;
    }
    BrokenTime.tm_mon = Month;
    BrokenTime.tm_mday = (int)Days + 1;
    BrokenTime.tm_isdst = 0;
    return &BrokenTime;
}

struct tm *localtime(const time_t *Timer) {
    /* 无 TZ / locale：墙上钟已是 UTC，localtime 与 gmtime 同形 */
    return gmtime(Timer);
}

char *asctime(const struct tm *Broken) {
    static const char *WeekdayNames[] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    static const char *MonthNames[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    int Weekday;
    int Month;

    if (!Broken) {
        return 0;
    }
    Weekday = Broken->tm_wday;
    Month = Broken->tm_mon;
    if (Weekday < 0 || Weekday > 6) {
        Weekday = 0;
    }
    if (Month < 0 || Month > 11) {
        Month = 0;
    }
    snprintf(AsctimeBuffer, sizeof(AsctimeBuffer),
             "%s %s %2d %02d:%02d:%02d %d\n", WeekdayNames[Weekday],
             MonthNames[Month], Broken->tm_mday, Broken->tm_hour,
             Broken->tm_min, Broken->tm_sec, Broken->tm_year + 1900);
    return AsctimeBuffer;
}

size_t strftime(char *Destination, size_t Capacity, const char *Format,
                const struct tm *Broken) {
    /* 认：%Y %m %d %H %M %S %a %b %w %%；其余原样 */
    static const char *WeekdayShort[] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    static const char *MonthShort[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    size_t Written = 0;
    const char *Cursor;
    int Weekday;
    int Month;

    if (!Destination || !Format || !Broken || Capacity == 0) {
        return 0;
    }
    Weekday = Broken->tm_wday;
    Month = Broken->tm_mon;
    if (Weekday < 0 || Weekday > 6) {
        Weekday = 0;
    }
    if (Month < 0 || Month > 11) {
        Month = 0;
    }
    for (Cursor = Format; *Cursor != 0 && Written + 1 < Capacity; Cursor++) {
        char Field[16];
        int FieldLength = 0;

        if (*Cursor != '%') {
            Destination[Written++] = *Cursor;
            continue;
        }
        Cursor++;
        if (*Cursor == 0) {
            break;
        }
        if (*Cursor == '%') {
            Destination[Written++] = '%';
            continue;
        }
        if (*Cursor == 'Y') {
            FieldLength =
                snprintf(Field, sizeof(Field), "%d", Broken->tm_year + 1900);
        } else if (*Cursor == 'm') {
            FieldLength =
                snprintf(Field, sizeof(Field), "%02d", Broken->tm_mon + 1);
        } else if (*Cursor == 'd') {
            FieldLength =
                snprintf(Field, sizeof(Field), "%02d", Broken->tm_mday);
        } else if (*Cursor == 'H') {
            FieldLength =
                snprintf(Field, sizeof(Field), "%02d", Broken->tm_hour);
        } else if (*Cursor == 'M') {
            FieldLength =
                snprintf(Field, sizeof(Field), "%02d", Broken->tm_min);
        } else if (*Cursor == 'S') {
            FieldLength =
                snprintf(Field, sizeof(Field), "%02d", Broken->tm_sec);
        } else if (*Cursor == 'a') {
            FieldLength = snprintf(Field, sizeof(Field), "%s", WeekdayShort[Weekday]);
        } else if (*Cursor == 'b') {
            FieldLength = snprintf(Field, sizeof(Field), "%s", MonthShort[Month]);
        } else if (*Cursor == 'w') {
            FieldLength = snprintf(Field, sizeof(Field), "%d", Weekday);
        } else {
            if (Written + 2 >= Capacity) {
                break;
            }
            Destination[Written++] = '%';
            Destination[Written++] = *Cursor;
            continue;
        }
        if (FieldLength < 0) {
            break;
        }
        if (Written + (size_t)FieldLength >= Capacity) {
            break;
        }
        memcpy(Destination + Written, Field, (size_t)FieldLength);
        Written += (size_t)FieldLength;
    }
    Destination[Written] = 0;
    return Written;
}
