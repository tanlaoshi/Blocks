/*
 * math.c — fabs / floor / ceil / sqrt / pow / fmod（教学软浮点）
 *
 * floor/ceil/fmod 不用 long 截断（大数与 LONG_MIN 边界会翻车）。
 */
#include <math.h>

static double truncate_toward_zero(double value) {
    if (value >= 0.0) {
        return (double)(unsigned long long)value;
    }
    return -(double)(unsigned long long)(-value);
}

double fabs(double value) {
    return value < 0.0 ? -value : value;
}

double floor(double value) {
    double truncated = truncate_toward_zero(value);

    if (value < 0.0 && truncated != value) {
        return truncated - 1.0;
    }
    return truncated;
}

double ceil(double value) {
    double truncated = truncate_toward_zero(value);

    if (value > 0.0 && truncated != value) {
        return truncated + 1.0;
    }
    return truncated;
}

double fmod(double dividend, double divisor) {
    double quotient;

    if (divisor == 0.0) {
        return 0.0;
    }
    quotient = truncate_toward_zero(dividend / divisor);
    return dividend - quotient * divisor;
}

double sqrt(double value) {
    double guess;
    int step;

    if (value < 0.0) {
        return 0.0;
    }
    if (value == 0.0) {
        return 0.0;
    }
    guess = value;
    if (guess < 1.0) {
        guess = 1.0;
    }
    for (step = 0; step < 32; step++) {
        guess = 0.5 * (guess + value / guess);
    }
    return guess;
}

double pow(double base, double exponent) {
    int steps;
    int index;
    double product;
    int negative_exponent = 0;

    if (exponent == 0.0) {
        return 1.0;
    }
    steps = (int)exponent;
    if ((double)steps != exponent) {
        return 0.0;
    }
    if (steps < 0) {
        negative_exponent = 1;
        steps = -steps;
    }
    product = 1.0;
    for (index = 0; index < steps; index++) {
        product *= base;
    }
    if (negative_exponent) {
        return (product == 0.0) ? 0.0 : (1.0 / product);
    }
    return product;
}
