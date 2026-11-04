/*
 * stdlib_parse.c — strtol / strtoul / strtod（展览级命名）
 */
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

static int DigitValue(int Character, int Base) {
    int Value;

    if (Character >= '0' && Character <= '9') {
        Value = Character - '0';
    } else if (Character >= 'a' && Character <= 'z') {
        Value = Character - 'a' + 10;
    } else if (Character >= 'A' && Character <= 'Z') {
        Value = Character - 'A' + 10;
    } else {
        return -1;
    }
    if (Value >= Base) {
        return -1;
    }
    return Value;
}

static unsigned long ParseUnsigned(
    const char *Text,
    char **EndPointer,
    int Base,
    int *NegativeOut,
    int *ConvertedOut
) {
    const char *Cursor = Text;
    unsigned long Accumulator = 0;
    int Negative = 0;
    int Converted = 0;
    int Digit;
    unsigned long Cutoff;
    unsigned long CutLimit;

    if (NegativeOut) {
        *NegativeOut = 0;
    }
    if (ConvertedOut) {
        *ConvertedOut = 0;
    }
    if (!Cursor) {
        if (EndPointer) {
            *EndPointer = (char *)Text;
        }
        return 0;
    }
    while (isspace((unsigned char)*Cursor)) {
        Cursor++;
    }
    if (*Cursor == '+' || *Cursor == '-') {
        Negative = (*Cursor == '-');
        Cursor++;
    }
    if (Base == 0) {
        if (Cursor[0] == '0' && (Cursor[1] == 'x' || Cursor[1] == 'X')) {
            Base = 16;
        } else if (Cursor[0] == '0') {
            Base = 8;
        } else {
            Base = 10;
        }
    }
    if (Base == 16 && Cursor[0] == '0' &&
        (Cursor[1] == 'x' || Cursor[1] == 'X')) {
        Cursor += 2;
    }
    if (Base < 2 || Base > 36) {
        errno = EINVAL;
        if (EndPointer) {
            *EndPointer = (char *)Text;
        }
        return 0;
    }

    Cutoff = ULONG_MAX / (unsigned long)Base;
    CutLimit = ULONG_MAX % (unsigned long)Base;
    for (;;) {
        Digit = DigitValue((unsigned char)*Cursor, Base);
        if (Digit < 0) {
            break;
        }
        if (Accumulator > Cutoff ||
            (Accumulator == Cutoff && (unsigned long)Digit > CutLimit)) {
            Converted = 1;
            Accumulator = ULONG_MAX;
            errno = ERANGE;
            Cursor++;
            while (DigitValue((unsigned char)*Cursor, Base) >= 0) {
                Cursor++;
            }
            break;
        }
        Accumulator =
            Accumulator * (unsigned long)Base + (unsigned long)Digit;
        Converted = 1;
        Cursor++;
    }
    if (!Converted) {
        if (EndPointer) {
            *EndPointer = (char *)Text;
        }
        return 0;
    }
    if (EndPointer) {
        *EndPointer = (char *)Cursor;
    }
    if (NegativeOut) {
        *NegativeOut = Negative;
    }
    if (ConvertedOut) {
        *ConvertedOut = Converted;
    }
    return Accumulator;
}

unsigned long strtoul(const char *Text, char **EndPointer, int Base) {
    int Negative = 0;
    unsigned long Accumulator;

    Accumulator = ParseUnsigned(Text, EndPointer, Base, &Negative, 0);
    if (Negative) {
        return (unsigned long)(-(long)Accumulator);
    }
    return Accumulator;
}

long strtol(const char *Text, char **EndPointer, int Base) {
    int Negative = 0;
    unsigned long Accumulator;
    int Converted = 0;

    Accumulator = ParseUnsigned(Text, EndPointer, Base, &Negative, &Converted);
    if (!Converted) {
        return 0;
    }
    if (Negative) {
        if (Accumulator > (unsigned long)LONG_MAX + 1ul) {
            errno = ERANGE;
            return LONG_MIN;
        }
        return -(long)Accumulator;
    }
    if (Accumulator > (unsigned long)LONG_MAX) {
        errno = ERANGE;
        return LONG_MAX;
    }
    return (long)Accumulator;
}

double strtod(const char *Text, char **EndPointer) {
    const char *Cursor = Text;
    int Negative = 0;
    double Accumulator = 0.0;
    double Fraction = 0.0;
    double Place = 0.1;
    int Converted = 0;
    int Exponent = 0;
    int ExponentNegative = 0;
    int ExponentConverted = 0;
    int Step;
    double Scale;

    if (!Cursor) {
        if (EndPointer) {
            *EndPointer = (char *)Text;
        }
        return 0.0;
    }
    while (isspace((unsigned char)*Cursor)) {
        Cursor++;
    }
    if (*Cursor == '+' || *Cursor == '-') {
        Negative = (*Cursor == '-');
        Cursor++;
    }
    while (isdigit((unsigned char)*Cursor)) {
        Accumulator = Accumulator * 10.0 + (double)(*Cursor - '0');
        Converted = 1;
        Cursor++;
    }
    if (*Cursor == '.') {
        Cursor++;
        while (isdigit((unsigned char)*Cursor)) {
            Fraction += Place * (double)(*Cursor - '0');
            Place *= 0.1;
            Converted = 1;
            Cursor++;
        }
    }
    Accumulator += Fraction;
    if (*Cursor == 'e' || *Cursor == 'E') {
        const char *ExponentMark = Cursor;

        Cursor++;
        if (*Cursor == '+' || *Cursor == '-') {
            ExponentNegative = (*Cursor == '-');
            Cursor++;
        }
        while (isdigit((unsigned char)*Cursor)) {
            Exponent = Exponent * 10 + (*Cursor - '0');
            ExponentConverted = 1;
            Cursor++;
        }
        if (!ExponentConverted) {
            Cursor = ExponentMark;
        } else if (ExponentNegative) {
            Exponent = -Exponent;
        }
    }
    if (!Converted) {
        if (EndPointer) {
            *EndPointer = (char *)Text;
        }
        return 0.0;
    }
    Scale = 1.0;
    if (Exponent > 0) {
        for (Step = 0; Step < Exponent; Step++) {
            Scale *= 10.0;
        }
        Accumulator *= Scale;
    } else if (Exponent < 0) {
        for (Step = 0; Step < -Exponent; Step++) {
            Scale *= 10.0;
        }
        Accumulator /= Scale;
    }
    if (EndPointer) {
        *EndPointer = (char *)Cursor;
    }
    return Negative ? -Accumulator : Accumulator;
}
