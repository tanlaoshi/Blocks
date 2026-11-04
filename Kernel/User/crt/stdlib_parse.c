/*
 * stdlib_parse.c — strtol / strtoul / strtod（libc-stdlib-parse）
 */
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

static int DigitValue(int C, int Base) {
    int V;

    if (C >= '0' && C <= '9') {
        V = C - '0';
    } else if (C >= 'a' && C <= 'z') {
        V = C - 'a' + 10;
    } else if (C >= 'A' && C <= 'Z') {
        V = C - 'A' + 10;
    } else {
        return -1;
    }
    if (V >= Base) {
        return -1;
    }
    return V;
}

static unsigned long ParseUnsigned(
    const char *Nptr,
    char **Endptr,
    int Base,
    int *NegOut,
    int *AnyOut
) {
    const char *S = Nptr;
    const char *StartDigits;
    unsigned long Acc = 0;
    int Neg = 0;
    int Any = 0;
    int Digit;
    unsigned long Cutoff;
    unsigned long Cutlim;

    if (NegOut) {
        *NegOut = 0;
    }
    if (AnyOut) {
        *AnyOut = 0;
    }
    if (!S) {
        if (Endptr) {
            *Endptr = (char *)Nptr;
        }
        return 0;
    }
    while (isspace((unsigned char)*S)) {
        S++;
    }
    if (*S == '+' || *S == '-') {
        Neg = (*S == '-');
        S++;
    }
    if (Base == 0) {
        if (S[0] == '0' && (S[1] == 'x' || S[1] == 'X')) {
            Base = 16;
        } else if (S[0] == '0') {
            Base = 8;
        } else {
            Base = 10;
        }
    }
    if (Base == 16 && S[0] == '0' && (S[1] == 'x' || S[1] == 'X')) {
        S += 2;
    }
    if (Base < 2 || Base > 36) {
        errno = EINVAL;
        if (Endptr) {
            *Endptr = (char *)Nptr;
        }
        return 0;
    }

    Cutoff = ULONG_MAX / (unsigned long)Base;
    Cutlim = ULONG_MAX % (unsigned long)Base;
    StartDigits = S;
    for (;;) {
        Digit = DigitValue((unsigned char)*S, Base);
        if (Digit < 0) {
            break;
        }
        if (Acc > Cutoff || (Acc == Cutoff && (unsigned long)Digit > Cutlim)) {
            Any = 1;
            Acc = ULONG_MAX;
            errno = ERANGE;
            S++;
            while (DigitValue((unsigned char)*S, Base) >= 0) {
                S++;
            }
            break;
        }
        Acc = Acc * (unsigned long)Base + (unsigned long)Digit;
        Any = 1;
        S++;
    }
    if (!Any) {
        if (Endptr) {
            *Endptr = (char *)Nptr;
        }
        return 0;
    }
    if (Endptr) {
        *Endptr = (char *)S;
    }
    if (NegOut) {
        *NegOut = Neg;
    }
    if (AnyOut) {
        *AnyOut = Any;
    }
    (void)StartDigits;
    return Acc;
}

unsigned long strtoul(const char *nptr, char **endptr, int base) {
    int Neg = 0;
    unsigned long Acc;

    Acc = ParseUnsigned(nptr, endptr, base, &Neg, 0);
    if (Neg) {
        return (unsigned long)(-(long)Acc);
    }
    return Acc;
}

long strtol(const char *nptr, char **endptr, int base) {
    int Neg = 0;
    unsigned long Acc;
    int Any = 0;

    Acc = ParseUnsigned(nptr, endptr, base, &Neg, &Any);
    if (!Any) {
        return 0;
    }
    if (Neg) {
        if (Acc > (unsigned long)LONG_MAX + 1ul) {
            errno = ERANGE;
            return LONG_MIN;
        }
        return -(long)Acc;
    }
    if (Acc > (unsigned long)LONG_MAX) {
        errno = ERANGE;
        return LONG_MAX;
    }
    return (long)Acc;
}

double strtod(const char *nptr, char **endptr) {
    const char *S = nptr;
    int Neg = 0;
    double Acc = 0.0;
    double Frac = 0.0;
    double Place = 0.1;
    int Any = 0;
    int Exp = 0;
    int ExpNeg = 0;
    int ExpAny = 0;
    int E;
    double Scale;

    if (!S) {
        if (endptr) {
            *endptr = (char *)nptr;
        }
        return 0.0;
    }
    while (isspace((unsigned char)*S)) {
        S++;
    }
    if (*S == '+' || *S == '-') {
        Neg = (*S == '-');
        S++;
    }
    while (isdigit((unsigned char)*S)) {
        Acc = Acc * 10.0 + (double)(*S - '0');
        Any = 1;
        S++;
    }
    if (*S == '.') {
        S++;
        while (isdigit((unsigned char)*S)) {
            Frac += Place * (double)(*S - '0');
            Place *= 0.1;
            Any = 1;
            S++;
        }
    }
    Acc += Frac;
    if (*S == 'e' || *S == 'E') {
        const char *ExpMark = S;
        S++;
        if (*S == '+' || *S == '-') {
            ExpNeg = (*S == '-');
            S++;
        }
        while (isdigit((unsigned char)*S)) {
            Exp = Exp * 10 + (*S - '0');
            ExpAny = 1;
            S++;
        }
        if (!ExpAny) {
            S = ExpMark;
        } else if (ExpNeg) {
            Exp = -Exp;
        }
    }
    if (!Any) {
        if (endptr) {
            *endptr = (char *)nptr;
        }
        return 0.0;
    }
    Scale = 1.0;
    if (Exp > 0) {
        for (E = 0; E < Exp; E++) {
            Scale *= 10.0;
        }
        Acc *= Scale;
    } else if (Exp < 0) {
        for (E = 0; E < -Exp; E++) {
            Scale *= 10.0;
        }
        Acc /= Scale;
    }
    if (endptr) {
        *endptr = (char *)S;
    }
    return Neg ? -Acc : Acc;
}
