/*
 * math.c — fabs / floor / ceil / sqrt / pow / fmod（教学软浮点）
 *
 * floor/ceil/fmod 不用 long 截断（大数与 LONG_MIN 边界会翻车）。
 */
#include <math.h>

static double TruncateTowardZero(double Value) {
    if (Value >= 0.0) {
        return (double)(unsigned long long)Value;
    }
    return -(double)(unsigned long long)(-Value);
}

double fabs(double Value) {
    return Value < 0.0 ? -Value : Value;
}

double floor(double Value) {
    double Truncated = TruncateTowardZero(Value);

    if (Value < 0.0 && Truncated != Value) {
        return Truncated - 1.0;
    }
    return Truncated;
}

double ceil(double Value) {
    double Truncated = TruncateTowardZero(Value);

    if (Value > 0.0 && Truncated != Value) {
        return Truncated + 1.0;
    }
    return Truncated;
}

double fmod(double Dividend, double Divisor) {
    double Quotient;

    if (Divisor == 0.0) {
        return 0.0;
    }
    Quotient = TruncateTowardZero(Dividend / Divisor);
    return Dividend - Quotient * Divisor;
}

double sqrt(double Value) {
    double Guess;
    int Step;

    if (Value < 0.0) {
        return 0.0;
    }
    if (Value == 0.0) {
        return 0.0;
    }
    Guess = Value;
    if (Guess < 1.0) {
        Guess = 1.0;
    }
    for (Step = 0; Step < 32; Step++) {
        Guess = 0.5 * (Guess + Value / Guess);
    }
    return Guess;
}

double pow(double Base, double Exponent) {
    int Steps;
    int Index;
    double Product;
    int NegativeExponent = 0;

    if (Exponent == 0.0) {
        return 1.0;
    }
    Steps = (int)Exponent;
    if ((double)Steps != Exponent) {
        return 0.0;
    }
    if (Steps < 0) {
        NegativeExponent = 1;
        Steps = -Steps;
    }
    Product = 1.0;
    for (Index = 0; Index < Steps; Index++) {
        Product *= Base;
    }
    if (NegativeExponent) {
        return (Product == 0.0) ? 0.0 : (1.0 / Product);
    }
    return Product;
}
