/*
 * time.c — clock / time / 最小 gmtime/asctime/strftime（全称下划线）
 *
 * time()：SYS_TIME_WALL → UTC Unix 秒；失败 (time_t)-1（C99）。
 * clock()：仍用单调 SYS_CLOCK_MS。
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <Host/UnixStandard.h>
#include <Blocks/Syscall.h>

clock_t clock(void) {
    return (clock_t)clock_ms();
}

time_t time(time_t *out) {
    long wall_seconds = blocks_time_wall();
    time_t now;

    if (wall_seconds < 0) {
        return (time_t)-1;
    }
    now = (time_t)wall_seconds;
    if (out) {
        *out = now;
    }
    return now;
}

static struct tm broken_time;
static char asctime_buffer[32];

struct tm *gmtime(const time_t *timer) {
    time_t instant;
    long days;
    long seconds_of_day;
    int year;
    int year_length;
    static const int month_days[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    int month;
    int leap_year;
    int days_in_month;

    if (!timer) {
        return 0;
    }
    instant = *timer;
    if (instant < 0) {
        instant = 0;
    }
    days = instant / 86400L;
    seconds_of_day = instant % 86400L;
    broken_time.tm_hour = (int)(seconds_of_day / 3600L);
    broken_time.tm_min = (int)((seconds_of_day % 3600L) / 60L);
    broken_time.tm_sec = (int)(seconds_of_day % 60L);
    broken_time.tm_wday = (int)((days + 4) % 7); /* 1970-01-01 Thursday */
    year = 1970;
    for (;;) {
        leap_year =
            ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0));
        year_length = leap_year ? 366 : 365;
        if (days < year_length) {
            break;
        }
        days -= year_length;
        year++;
    }
    broken_time.tm_year = year - 1900;
    broken_time.tm_yday = (int)days;
    leap_year = ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0));
    for (month = 0; month < 12; month++) {
        days_in_month = month_days[month];
        if (month == 1 && leap_year) {
            days_in_month = 29;
        }
        if (days < days_in_month) {
            break;
        }
        days -= days_in_month;
    }
    broken_time.tm_mon = month;
    broken_time.tm_mday = (int)days + 1;
    broken_time.tm_isdst = 0;
    return &broken_time;
}

struct tm *localtime(const time_t *timer) {
    /* 无 TZ / locale：墙上钟已是 UTC，localtime 与 gmtime 同形 */
    return gmtime(timer);
}

char *asctime(const struct tm *broken) {
    static const char *weekday_names[] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    static const char *month_names[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    int weekday;
    int month;

    if (!broken) {
        return 0;
    }
    weekday = broken->tm_wday;
    month = broken->tm_mon;
    if (weekday < 0 || weekday > 6) {
        weekday = 0;
    }
    if (month < 0 || month > 11) {
        month = 0;
    }
    snprintf(asctime_buffer, sizeof(asctime_buffer),
             "%s %s %2d %02d:%02d:%02d %d\n", weekday_names[weekday],
             month_names[month], broken->tm_mday, broken->tm_hour,
             broken->tm_min, broken->tm_sec, broken->tm_year + 1900);
    return asctime_buffer;
}

size_t strftime(char *destination, size_t capacity, const char *format,
                const struct tm *broken) {
    /* 认：%Y %m %d %H %M %S %a %b %w %%；其余原样 */
    static const char *weekday_short[] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    static const char *month_short[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    size_t written = 0;
    const char *cursor;
    int weekday;
    int month;

    if (!destination || !format || !broken || capacity == 0) {
        return 0;
    }
    weekday = broken->tm_wday;
    month = broken->tm_mon;
    if (weekday < 0 || weekday > 6) {
        weekday = 0;
    }
    if (month < 0 || month > 11) {
        month = 0;
    }
    for (cursor = format; *cursor != 0 && written + 1 < capacity; cursor++) {
        char field[16];
        int field_length = 0;

        if (*cursor != '%') {
            destination[written++] = *cursor;
            continue;
        }
        cursor++;
        if (*cursor == 0) {
            break;
        }
        if (*cursor == '%') {
            destination[written++] = '%';
            continue;
        }
        if (*cursor == 'Y') {
            field_length =
                snprintf(field, sizeof(field), "%d", broken->tm_year + 1900);
        } else if (*cursor == 'm') {
            field_length =
                snprintf(field, sizeof(field), "%02d", broken->tm_mon + 1);
        } else if (*cursor == 'd') {
            field_length =
                snprintf(field, sizeof(field), "%02d", broken->tm_mday);
        } else if (*cursor == 'H') {
            field_length =
                snprintf(field, sizeof(field), "%02d", broken->tm_hour);
        } else if (*cursor == 'M') {
            field_length =
                snprintf(field, sizeof(field), "%02d", broken->tm_min);
        } else if (*cursor == 'S') {
            field_length =
                snprintf(field, sizeof(field), "%02d", broken->tm_sec);
        } else if (*cursor == 'a') {
            field_length = snprintf(field, sizeof(field), "%s", weekday_short[weekday]);
        } else if (*cursor == 'b') {
            field_length = snprintf(field, sizeof(field), "%s", month_short[month]);
        } else if (*cursor == 'w') {
            field_length = snprintf(field, sizeof(field), "%d", weekday);
        } else {
            if (written + 2 >= capacity) {
                break;
            }
            destination[written++] = '%';
            destination[written++] = *cursor;
            continue;
        }
        if (field_length < 0) {
            break;
        }
        if (written + (size_t)field_length >= capacity) {
            break;
        }
        memcpy(destination + written, field, (size_t)field_length);
        written += (size_t)field_length;
    }
    destination[written] = 0;
    return written;
}
