/*
 * time.h — 薄封装（libc-time）
 * clock()：单调 SYS_CLOCK_MS；time()：SYS_TIME_WALL（UTC Unix 秒）
 */
#ifndef TIME_H
#define TIME_H

#include <stddef.h>

typedef long time_t;
typedef long clock_t;

#define CLOCKS_PER_SEC 1000L

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

clock_t clock(void);
time_t time(time_t *t);
double difftime(time_t time1, time_t time0);
char *asctime(const struct tm *tm);
struct tm *gmtime(const time_t *timer);
/* Blocks 无时区库：localtime ≡ gmtime（UTC） */
struct tm *localtime(const time_t *timer);
time_t mktime(struct tm *tm);
size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm);

#endif
