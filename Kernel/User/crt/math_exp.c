/*
 * math_exp.c — exp / log / log10（教学软浮点）
 *
 * log：先把真数收到 [0.5, 2)，再 artanh 级数，避免大数时 (x-1)/(x+1)→1 发散。
 */
#include <math.h>

#define MATH_LN2 0.693147180559945309417
#define MATH_LN10 2.30258509299404568402

static double ExpSeries(double Radians) {
    double Sum = 1.0;
    double Term = 1.0;
    int Step;

    for (Step = 1; Step < 24; Step++) {
        Term *= Radians / (double)Step;
        Sum += Term;
    }
    return Sum;
}

double exp(double Radians) {
    int Negative = 0;
    double Magnitude = Radians;
    int Halves;

    if (Magnitude < 0.0) {
        Negative = 1;
        Magnitude = -Magnitude;
    }
    /* e^x = (e^(x/2^k))^(2^k)；先压到较小再平方回去，减轻级数压力 */
    Halves = 0;
    while (Magnitude > 1.0 && Halves < 16) {
        Magnitude *= 0.5;
        Halves++;
    }
    {
        double Value = ExpSeries(Magnitude);
        int Step;

        for (Step = 0; Step < Halves; Step++) {
            Value *= Value;
        }
        if (Negative) {
            return (Value == 0.0) ? 0.0 : (1.0 / Value);
        }
        return Value;
    }
}

static double LogNearOne(double Scaled) {
    double Ratio = (Scaled - 1.0) / (Scaled + 1.0);
    double Square = Ratio * Ratio;
    double Term = Ratio;
    double Sum = Ratio;
    int Step;

    for (Step = 1; Step < 24; Step++) {
        Term *= Square;
        Sum += Term / (double)(2 * Step + 1);
    }
    return 2.0 * Sum;
}

double log(double Value) {
    int Exponent = 0;
    double Scaled = Value;

    if (Value <= 0.0) {
        return 0.0;
    }
    while (Scaled >= 2.0) {
        Scaled *= 0.5;
        Exponent++;
    }
    while (Scaled < 0.5) {
        Scaled *= 2.0;
        Exponent--;
    }
    return LogNearOne(Scaled) + (double)Exponent * MATH_LN2;
}

double log10(double Value) {
    if (Value <= 0.0) {
        return 0.0;
    }
    return log(Value) / MATH_LN10;
}
