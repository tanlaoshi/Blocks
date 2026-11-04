/*
 * stdio_scan_format.c — vsscanf 教学子集
 * 认：%d %i %u %ld %lu %x %X %o %c %s %f %lf %%；空白匹配；字面量
 */
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static int MatchLiteral(const char **StringCursor, char Expected) {
    if (**StringCursor != Expected) {
        return 0;
    }
    (*StringCursor)++;
    return 1;
}

static void SkipSpaces(const char **StringCursor) {
    while (isspace((unsigned char)**StringCursor)) {
        (*StringCursor)++;
    }
}

int BlocksVsscanf(const char *String, const char *Format, va_list Arguments) {
    const char *S = String;
    const char *F = Format;
    int Count = 0;
    int LengthLong;

    if (!String || !Format) {
        return -1;
    }
    while (*F) {
        if (*F == '%') {
            F++;
            LengthLong = 0;
            if (*F == 'l') {
                LengthLong = 1;
                F++;
            }
            if (*F == 'd' || *F == 'i' || *F == 'u' || *F == 'x' || *F == 'X' ||
                *F == 'o') {
                char *End = 0;
                int Base = 10;

                SkipSpaces(&S);
                if (*F == 'x' || *F == 'X') {
                    Base = 16;
                } else if (*F == 'o') {
                    Base = 8;
                }
                if (*F == 'u' || *F == 'x' || *F == 'X' || *F == 'o') {
                    unsigned long Value = strtoul(S, &End, Base);

                    if (End == S) {
                        break;
                    }
                    if (LengthLong) {
                        unsigned long *Out = va_arg(Arguments, unsigned long *);

                        if (Out) {
                            *Out = Value;
                        }
                    } else {
                        unsigned *Out = va_arg(Arguments, unsigned *);

                        if (Out) {
                            *Out = (unsigned)Value;
                        }
                    }
                    S = End;
                } else {
                    long Value = strtol(S, &End, Base);

                    if (End == S) {
                        break;
                    }
                    if (LengthLong) {
                        long *Out = va_arg(Arguments, long *);

                        if (Out) {
                            *Out = Value;
                        }
                    } else {
                        int *Out = va_arg(Arguments, int *);

                        if (Out) {
                            *Out = (int)Value;
                        }
                    }
                    S = End;
                }
                Count++;
                F++;
                continue;
            }
            if (*F == 'f') {
                char *End = 0;
                double Value;

                SkipSpaces(&S);
                Value = strtod(S, &End);
                if (End == S) {
                    break;
                }
                if (LengthLong) {
                    double *Out = va_arg(Arguments, double *);

                    if (Out) {
                        *Out = Value;
                    }
                } else {
                    float *Out = va_arg(Arguments, float *);

                    if (Out) {
                        *Out = (float)Value;
                    }
                }
                S = End;
                Count++;
                F++;
                continue;
            }
            if (*F == 'c') {
                char *Out = va_arg(Arguments, char *);

                if (!*S) {
                    break;
                }
                if (Out) {
                    *Out = *S;
                }
                S++;
                Count++;
                F++;
                continue;
            }
            if (*F == 's') {
                char *Out = va_arg(Arguments, char *);
                size_t N = 0;

                SkipSpaces(&S);
                if (!*S) {
                    break;
                }
                while (*S && !isspace((unsigned char)*S)) {
                    if (Out) {
                        Out[N] = *S;
                    }
                    N++;
                    S++;
                }
                if (Out) {
                    Out[N] = 0;
                }
                Count++;
                F++;
                continue;
            }
            if (*F == '%') {
                if (!MatchLiteral(&S, '%')) {
                    break;
                }
                F++;
                continue;
            }
            break;
        }
        if (isspace((unsigned char)*F)) {
            while (isspace((unsigned char)*F)) {
                F++;
            }
            SkipSpaces(&S);
            continue;
        }
        if (!MatchLiteral(&S, *F)) {
            break;
        }
        F++;
    }
    return Count;
}
