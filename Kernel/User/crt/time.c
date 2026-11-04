/*
 * time.c — clock / time / 最小 gmtime/asctime/strftime（libc-time）
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

clock_t clock(void) {
    return (clock_t)clock_ms();
}

time_t time(time_t *t) {
    time_t Now = (time_t)(clock_ms() / 1000ul);

    if (t) {
        *t = Now;
    }
    return Now;
}

static struct tm gBroken;
static char gAsc[32];

struct tm *gmtime(const time_t *timer) {
    time_t T;
    long Days;
    long Secs;
    int Year;
    int Yday;
    static const int Md[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int M;
    int Leap;

    if (!timer) {
        return 0;
    }
    T = *timer;
    if (T < 0) {
        T = 0;
    }
    Days = T / 86400L;
    Secs = T % 86400L;
    gBroken.tm_hour = (int)(Secs / 3600L);
    gBroken.tm_min = (int)((Secs % 3600L) / 60L);
    gBroken.tm_sec = (int)(Secs % 60L);
    gBroken.tm_wday = (int)((Days + 4) % 7); /* 1970-01-01 Thursday */
    Year = 1970;
    for (;;) {
        Leap = ((Year % 4 == 0 && Year % 100 != 0) || (Year % 400 == 0));
        Yday = Leap ? 366 : 365;
        if (Days < Yday) {
            break;
        }
        Days -= Yday;
        Year++;
    }
    gBroken.tm_year = Year - 1900;
    gBroken.tm_yday = (int)Days;
    Leap = ((Year % 4 == 0 && Year % 100 != 0) || (Year % 400 == 0));
    for (M = 0; M < 12; M++) {
        int Dim = Md[M];
        if (M == 1 && Leap) {
            Dim = 29;
        }
        if (Days < Dim) {
            break;
        }
        Days -= Dim;
    }
    gBroken.tm_mon = M;
    gBroken.tm_mday = (int)Days + 1;
    gBroken.tm_isdst = 0;
    return &gBroken;
}

char *asctime(const struct tm *tm) {
    static const char *Wday[] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    static const char *Mon[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    int W;
    int Mo;

    if (!tm) {
        return 0;
    }
    W = tm->tm_wday;
    Mo = tm->tm_mon;
    if (W < 0 || W > 6) {
        W = 0;
    }
    if (Mo < 0 || Mo > 11) {
        Mo = 0;
    }
    snprintf(gAsc, sizeof(gAsc), "%s %s %2d %02d:%02d:%02d %d\n",
             Wday[W], Mon[Mo], tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec,
             tm->tm_year + 1900);
    return gAsc;
}

size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm) {
    /* 最小：只认 %Y %m %d %H %M %S %%；其余原样 */
    size_t Out = 0;
    const char *P;

    if (!s || !fmt || !tm || max == 0) {
        return 0;
    }
    for (P = fmt; *P && Out + 1 < max; P++) {
        if (*P != '%') {
            s[Out++] = *P;
            continue;
        }
        P++;
        if (!*P) {
            break;
        }
        if (*P == '%') {
            s[Out++] = '%';
            continue;
        }
        {
            char Tmp[16];
            int N = 0;
            if (*P == 'Y') {
                N = snprintf(Tmp, sizeof(Tmp), "%d", tm->tm_year + 1900);
            } else if (*P == 'm') {
                N = snprintf(Tmp, sizeof(Tmp), "%02d", tm->tm_mon + 1);
            } else if (*P == 'd') {
                N = snprintf(Tmp, sizeof(Tmp), "%02d", tm->tm_mday);
            } else if (*P == 'H') {
                N = snprintf(Tmp, sizeof(Tmp), "%02d", tm->tm_hour);
            } else if (*P == 'M') {
                N = snprintf(Tmp, sizeof(Tmp), "%02d", tm->tm_min);
            } else if (*P == 'S') {
                N = snprintf(Tmp, sizeof(Tmp), "%02d", tm->tm_sec);
            } else {
                if (Out + 2 >= max) {
                    break;
                }
                s[Out++] = '%';
                s[Out++] = *P;
                continue;
            }
            if (N < 0) {
                break;
            }
            if (Out + (size_t)N >= max) {
                break;
            }
            memcpy(s + Out, Tmp, (size_t)N);
            Out += (size_t)N;
        }
    }
    s[Out] = 0;
    return Out;
}
