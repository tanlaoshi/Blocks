/*
 * math_exp.c — exp / log / log10（教学软浮点）
 *
 * log：先把真数收到 [0.5, 2)，再 artanh 级数，避免大数时 (x-1)/(x+1)→1 发散。
 */
#include <math.h>

#define MATH_LN2 0.693147180559945309417
#define MATH_LN10 2.30258509299404568402

static double exp_series(double radians) {
    double sum = 1.0;
    double term = 1.0;
    int step;

    for (step = 1; step < 24; step++) {
        term *= radians / (double)step;
        sum += term;
    }
    return sum;
}

double exp(double radians) {
    int negative = 0;
    double magnitude = radians;
    int halves;

    if (magnitude < 0.0) {
        negative = 1;
        magnitude = -magnitude;
    }
    /* e^x = (e^(x/2^k))^(2^k)；先压到较小再平方回去，减轻级数压力 */
    halves = 0;
    while (magnitude > 1.0 && halves < 16) {
        magnitude *= 0.5;
        halves++;
    }
    {
        double value = exp_series(magnitude);
        int step;

        for (step = 0; step < halves; step++) {
            value *= value;
        }
        if (negative) {
            return (value == 0.0) ? 0.0 : (1.0 / value);
        }
        return value;
    }
}

static double log_near_one(double scaled) {
    double ratio = (scaled - 1.0) / (scaled + 1.0);
    double square = ratio * ratio;
    double term = ratio;
    double sum = ratio;
    int step;

    for (step = 1; step < 24; step++) {
        term *= square;
        sum += term / (double)(2 * step + 1);
    }
    return 2.0 * sum;
}

double log(double value) {
    int exponent = 0;
    double scaled = value;

    if (value <= 0.0) {
        return 0.0;
    }
    while (scaled >= 2.0) {
        scaled *= 0.5;
        exponent++;
    }
    while (scaled < 0.5) {
        scaled *= 2.0;
        exponent--;
    }
    return log_near_one(scaled) + (double)exponent * MATH_LN2;
}

double log10(double value) {
    if (value <= 0.0) {
        return 0.0;
    }
    return log(value) / MATH_LN10;
}
