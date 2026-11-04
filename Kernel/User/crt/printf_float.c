/*
 * printf_float.c — 教学用 %f / %e / %g（展览级命名）
 */
#include <float.h>
#include <stddef.h>

static void PutCharacter(char *Destination, size_t Capacity, size_t *Position,
                         char Character) {
    if (Destination && Capacity > 0 && *Position + 1 < Capacity) {
        Destination[*Position] = Character;
    }
    (*Position)++;
}

static void PutString(char *Destination, size_t Capacity, size_t *Position,
                      const char *String) {
    while (*String != 0) {
        PutCharacter(Destination, Capacity, Position, *String++);
    }
}

static void PutUnsignedLong(char *Destination, size_t Capacity, size_t *Position,
                            unsigned long Value) {
    char Digits[32];
    int DigitCount = 0;

    if (Value == 0) {
        PutCharacter(Destination, Capacity, Position, '0');
        return;
    }
    while (Value > 0 && DigitCount < (int)sizeof(Digits)) {
        Digits[DigitCount++] = (char)('0' + (Value % 10ul));
        Value /= 10ul;
    }
    while (DigitCount > 0) {
        PutCharacter(Destination, Capacity, Position, Digits[--DigitCount]);
    }
}

static int IsNotANumber(double Value) {
    return Value != Value;
}

static int IsInfinity(double Value) {
    return (Value > DBL_MAX) || (Value < -DBL_MAX);
}

static double AbsoluteValue(double Value) {
    return Value < 0.0 ? -Value : Value;
}

static double PowerOfTen(int Exponent) {
    double Result = 1.0;
    int Index;

    if (Exponent >= 0) {
        for (Index = 0; Index < Exponent; Index++) {
            Result *= 10.0;
        }
    } else {
        for (Index = 0; Index < -Exponent; Index++) {
            Result /= 10.0;
        }
    }
    return Result;
}

/* 写入定点数（无指数）；返回已逻辑写入长度 */
static size_t FormatFixed(char *Destination, size_t Capacity, double Value,
                          int Precision, int *OutExponent) {
    size_t Position = 0;
    double Absolute;
    unsigned long IntegerPart;
    double Fraction;
    int Index;

    if (OutExponent) {
        *OutExponent = 0;
    }
    if (Value < 0.0) {
        PutCharacter(Destination, Capacity, &Position, '-');
        Value = -Value;
    }
    Absolute = Value;
    IntegerPart = (unsigned long)Absolute;
    Fraction = Absolute - (double)IntegerPart;
    PutUnsignedLong(Destination, Capacity, &Position, IntegerPart);
    if (Precision > 0) {
        unsigned long FractionPart;
        char FractionDigits[32];
        int DigitCount = Precision;

        PutCharacter(Destination, Capacity, &Position, '.');
        Fraction *= PowerOfTen(Precision);
        Fraction += 0.5; /* 简单四舍五入 */
        FractionPart = (unsigned long)Fraction;
        if (DigitCount > (int)sizeof(FractionDigits)) {
            DigitCount = (int)sizeof(FractionDigits);
        }
        for (Index = DigitCount - 1; Index >= 0; Index--) {
            FractionDigits[Index] = (char)('0' + (FractionPart % 10ul));
            FractionPart /= 10ul;
        }
        for (Index = 0; Index < DigitCount; Index++) {
            PutCharacter(Destination, Capacity, &Position, FractionDigits[Index]);
        }
    }
    return Position;
}

static size_t FormatScientific(char *Destination, size_t Capacity, double Value,
                               int Precision) {
    size_t Position = 0;
    int Exponent = 0;
    double Mantissa;
    char MantissaBuffer[64];
    size_t MantissaLength;
    size_t Index;

    if (Value < 0.0) {
        PutCharacter(Destination, Capacity, &Position, '-');
        Value = -Value;
    }
    if (Value == 0.0) {
        PutCharacter(Destination, Capacity, &Position, '0');
        if (Precision > 0) {
            PutCharacter(Destination, Capacity, &Position, '.');
            while (Precision-- > 0) {
                PutCharacter(Destination, Capacity, &Position, '0');
            }
        }
        PutString(Destination, Capacity, &Position, "e+00");
        return Position;
    }
    Mantissa = Value;
    while (Mantissa >= 10.0) {
        Mantissa /= 10.0;
        Exponent++;
    }
    while (Mantissa < 1.0) {
        Mantissa *= 10.0;
        Exponent--;
    }
    MantissaLength =
        FormatFixed(MantissaBuffer, sizeof(MantissaBuffer), Mantissa, Precision,
                    0);
    for (Index = 0; Index < MantissaLength; Index++) {
        PutCharacter(Destination, Capacity, &Position, MantissaBuffer[Index]);
    }
    PutCharacter(Destination, Capacity, &Position, 'e');
    if (Exponent < 0) {
        PutCharacter(Destination, Capacity, &Position, '-');
        Exponent = -Exponent;
    } else {
        PutCharacter(Destination, Capacity, &Position, '+');
    }
    if (Exponent < 10) {
        PutCharacter(Destination, Capacity, &Position, '0');
    }
    PutUnsignedLong(Destination, Capacity, &Position, (unsigned long)Exponent);
    return Position;
}

/*
 * Specifier: 'f'/'F'/'e'/'E'/'g'/'G'
 * Precision: <0 → 默认 6
 * 返回写入长度（不含 NUL）；Destination 可 NULL 仅计量
 */
size_t PrintfFormatDouble(char *Destination, size_t Capacity, double Value,
                          int Specifier, int Precision) {
    size_t Position = 0;
    int Lowercase =
        (Specifier == 'f' || Specifier == 'e' || Specifier == 'g');

    if (Precision < 0) {
        Precision = 6;
    }
    if (IsNotANumber(Value)) {
        PutString(Destination, Capacity, &Position, Lowercase ? "nan" : "NAN");
    } else if (IsInfinity(Value)) {
        if (Value < 0.0) {
            PutCharacter(Destination, Capacity, &Position, '-');
        }
        PutString(Destination, Capacity, &Position, Lowercase ? "inf" : "INF");
    } else if (Specifier == 'f' || Specifier == 'F') {
        Position = FormatFixed(Destination, Capacity, Value, Precision, 0);
    } else if (Specifier == 'e' || Specifier == 'E') {
        Position = FormatScientific(Destination, Capacity, Value, Precision);
        if (!Lowercase && Destination && Capacity > 0) {
            size_t Index;

            for (Index = 0; Index < Position && Index + 1 < Capacity; Index++) {
                if (Destination[Index] == 'e') {
                    Destination[Index] = 'E';
                }
            }
        }
    } else {
        /* %g：|Value| 很大或很小时用 e，否则 f；去尾零简化为固定精度 f */
        double Absolute = AbsoluteValue(Value);

        if (Absolute != 0.0 &&
            (Absolute < 1e-4 || Absolute >= PowerOfTen(Precision))) {
            Position = FormatScientific(Destination, Capacity, Value,
                                        Precision > 0 ? Precision - 1 : 0);
        } else {
            Position = FormatFixed(Destination, Capacity, Value, Precision, 0);
        }
    }
    if (Destination && Capacity > 0) {
        size_t End = (Position < Capacity) ? Position : Capacity - 1;

        Destination[End] = 0;
    }
    return Position;
}
