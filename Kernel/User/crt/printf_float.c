/*
 * printf_float.c — 教学用 %f / %e / %g（libc-printf-float）
 */
#include <float.h>
#include <stddef.h>

static void PutChar(char *Dst, size_t Cap, size_t *Pos, char C) {
    if (Dst && Cap > 0 && *Pos + 1 < Cap) {
        Dst[*Pos] = C;
    }
    (*Pos)++;
}

static void PutStr(char *Dst, size_t Cap, size_t *Pos, const char *S) {
    while (*S) {
        PutChar(Dst, Cap, Pos, *S++);
    }
}

static void PutUInt(char *Dst, size_t Cap, size_t *Pos, unsigned long V) {
    char Buf[32];
    int I = 0;

    if (V == 0) {
        PutChar(Dst, Cap, Pos, '0');
        return;
    }
    while (V > 0 && I < (int)sizeof(Buf)) {
        Buf[I++] = (char)('0' + (V % 10ul));
        V /= 10ul;
    }
    while (I > 0) {
        PutChar(Dst, Cap, Pos, Buf[--I]);
    }
}

static int IsNan(double X) {
    return X != X;
}

static int IsInf(double X) {
    return (X > DBL_MAX) || (X < -DBL_MAX);
}

static double Abs(double X) {
    return X < 0.0 ? -X : X;
}

static double Pow10(int E) {
    double R = 1.0;
    int I;

    if (E >= 0) {
        for (I = 0; I < E; I++) {
            R *= 10.0;
        }
    } else {
        for (I = 0; I < -E; I++) {
            R /= 10.0;
        }
    }
    return R;
}

/* 写入定点数（无指数）；返回已逻辑写入长度 */
static size_t FormatFixed(
    char *Dst,
    size_t Cap,
    double Value,
    int Prec,
    int *OutExp
) {
    size_t Pos = 0;
    double A;
    unsigned long Ip;
    double Frac;
    int I;

    if (OutExp) {
        *OutExp = 0;
    }
    if (Value < 0.0) {
        PutChar(Dst, Cap, &Pos, '-');
        Value = -Value;
    }
    A = Value;
    if (A < 1.0 && A > 0.0) {
        /* 仍按 fixed：0.xxx */
    }
    Ip = (unsigned long)A;
    Frac = A - (double)Ip;
    PutUInt(Dst, Cap, &Pos, Ip);
    if (Prec > 0) {
        PutChar(Dst, Cap, &Pos, '.');
        Frac *= Pow10(Prec);
        /* 简单四舍五入 */
        Frac += 0.5;
        {
            unsigned long Fp = (unsigned long)Frac;
            char Digs[32];
            int N = Prec;

            if (N > (int)sizeof(Digs)) {
                N = (int)sizeof(Digs);
            }
            for (I = N - 1; I >= 0; I--) {
                Digs[I] = (char)('0' + (Fp % 10ul));
                Fp /= 10ul;
            }
            for (I = 0; I < N; I++) {
                PutChar(Dst, Cap, &Pos, Digs[I]);
            }
        }
    }
    return Pos;
}

static size_t FormatSci(char *Dst, size_t Cap, double Value, int Prec) {
    size_t Pos = 0;
    int Exp = 0;
    double M;
    int NegExp = 0;

    if (Value < 0.0) {
        PutChar(Dst, Cap, &Pos, '-');
        Value = -Value;
    }
    if (Value == 0.0) {
        PutChar(Dst, Cap, &Pos, '0');
        if (Prec > 0) {
            PutChar(Dst, Cap, &Pos, '.');
            while (Prec-- > 0) {
                PutChar(Dst, Cap, &Pos, '0');
            }
        }
        PutStr(Dst, Cap, &Pos, "e+00");
        return Pos;
    }
    M = Value;
    while (M >= 10.0) {
        M /= 10.0;
        Exp++;
    }
    while (M < 1.0) {
        M *= 10.0;
        Exp--;
    }
    {
        char Mant[64];
        size_t MantLen = FormatFixed(Mant, sizeof(Mant), M, Prec, 0);
        size_t I;

        for (I = 0; I < MantLen; I++) {
            PutChar(Dst, Cap, &Pos, Mant[I]);
        }
    }
    PutChar(Dst, Cap, &Pos, 'e');
    if (Exp < 0) {
        PutChar(Dst, Cap, &Pos, '-');
        NegExp = 1;
        Exp = -Exp;
    } else {
        PutChar(Dst, Cap, &Pos, '+');
    }
    if (Exp < 10) {
        PutChar(Dst, Cap, &Pos, '0');
    }
    PutUInt(Dst, Cap, &Pos, (unsigned long)Exp);
    (void)NegExp;
    return Pos;
}

/*
 * Spec: 'f'/'F'/'e'/'E'/'g'/'G'
 * Prec: <0 → 默认 6
 * 返回写入长度（不含 NUL）；Dst 可 NULL 仅计量
 */
size_t PrintfFormatDouble(char *Dst, size_t Cap, double Value, int Spec, int Prec) {
    size_t Pos = 0;
    int Lower = (Spec == 'f' || Spec == 'e' || Spec == 'g');

    if (Prec < 0) {
        Prec = 6;
    }
    if (IsNan(Value)) {
        PutStr(Dst, Cap, &Pos, Lower ? "nan" : "NAN");
    } else if (IsInf(Value)) {
        if (Value < 0.0) {
            PutChar(Dst, Cap, &Pos, '-');
        }
        PutStr(Dst, Cap, &Pos, Lower ? "inf" : "INF");
    } else if (Spec == 'f' || Spec == 'F') {
        Pos = FormatFixed(Dst, Cap, Value, Prec, 0);
    } else if (Spec == 'e' || Spec == 'E') {
        Pos = FormatSci(Dst, Cap, Value, Prec);
        if (!Lower && Dst && Cap > 0) {
            size_t I;
            for (I = 0; I < Pos && I + 1 < Cap; I++) {
                if (Dst[I] == 'e') {
                    Dst[I] = 'E';
                }
            }
        }
    } else {
        /* %g：|Value| 很大或很小时用 e，否则 f；去尾零简化为固定 6 位 f */
        double A = Abs(Value);
        if (A != 0.0 && (A < 1e-4 || A >= Pow10(Prec))) {
            Pos = FormatSci(Dst, Cap, Value, Prec > 0 ? Prec - 1 : 0);
        } else {
            Pos = FormatFixed(Dst, Cap, Value, Prec, 0);
        }
    }
    if (Dst && Cap > 0) {
        size_t End = (Pos < Cap) ? Pos : Cap - 1;
        Dst[End] = 0;
    }
    return Pos;
}
