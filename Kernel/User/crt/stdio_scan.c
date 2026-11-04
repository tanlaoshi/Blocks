/*
 * stdio_scan.c — fgetc / fgets / getchar / sscanf 最小（hosted-stdio-scan）
 */
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int fgetc(FILE *fp) {
    unsigned char Ch;
    size_t N;

    if (!fp) {
        return EOF;
    }
    N = fread(&Ch, 1, 1, fp);
    if (N != 1) {
        return EOF;
    }
    return (int)Ch;
}

int getchar(void) {
    return fgetc(stdin);
}

char *fgets(char *s, int size, FILE *fp) {
    int I;
    int C;

    if (!s || size <= 0 || !fp) {
        return 0;
    }
    for (I = 0; I < size - 1; I++) {
        C = fgetc(fp);
        if (C == EOF) {
            if (I == 0) {
                return 0;
            }
            break;
        }
        s[I] = (char)C;
        if (C == '\n') {
            I++;
            break;
        }
    }
    s[I] = 0;
    return s;
}

int sscanf(const char *str, const char *fmt, ...) {
    va_list Ap;
    const char *S = str;
    const char *F = fmt;
    int Count = 0;

    if (!str || !fmt) {
        return EOF;
    }
    va_start(Ap, fmt);
    while (*F) {
        if (*F == '%') {
            F++;
            if (*F == 'd' || *F == 'i' || *F == 'u') {
                char *End = 0;
                while (isspace((unsigned char)*S)) {
                    S++;
                }
                if (*F == 'u') {
                    unsigned long V = strtoul(S, &End, 10);
                    unsigned *Out = va_arg(Ap, unsigned *);

                    if (End == S) {
                        break;
                    }
                    if (Out) {
                        *Out = (unsigned)V;
                    }
                    S = End;
                } else {
                    long V = strtol(S, &End, 10);
                    int *Out = va_arg(Ap, int *);

                    if (End == S) {
                        break;
                    }
                    if (Out) {
                        *Out = (int)V;
                    }
                    S = End;
                }
                Count++;
                F++;
                continue;
            }
            if (*F == 's') {
                char *Out = va_arg(Ap, char *);
                size_t N = 0;

                while (isspace((unsigned char)*S)) {
                    S++;
                }
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
                if (*S != '%') {
                    break;
                }
                S++;
                F++;
                continue;
            }
            break;
        }
        if (isspace((unsigned char)*F)) {
            while (isspace((unsigned char)*F)) {
                F++;
            }
            while (isspace((unsigned char)*S)) {
                S++;
            }
            continue;
        }
        if (*S != *F) {
            break;
        }
        S++;
        F++;
    }
    va_end(Ap);
    return Count;
}
