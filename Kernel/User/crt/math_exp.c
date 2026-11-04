/*
 * math_exp.c — exp / log / log10（教学软浮点）
 */
#include <math.h>

double exp(double x) {
    double Sum;
    double Term;
    int N;
    int Neg = 0;

    if (x < 0.0) {
        Neg = 1;
        x = -x;
    }
    if (x > 20.0) {
        /* 防溢：教学截断 */
        x = 20.0;
    }
    Sum = 1.0;
    Term = 1.0;
    for (N = 1; N < 24; N++) {
        Term *= x / (double)N;
        Sum += Term;
    }
    if (Neg) {
        if (Sum == 0.0) {
            return 0.0;
        }
        return 1.0 / Sum;
    }
    return Sum;
}

double log(double x) {
    double Y;
    double Y2;
    double Term;
    double Sum;
    int N;

    if (x <= 0.0) {
        return 0.0;
    }
    /* log(x) = 2 * artanh((x-1)/(x+1)) */
    Y = (x - 1.0) / (x + 1.0);
    Y2 = Y * Y;
    Term = Y;
    Sum = Y;
    for (N = 1; N < 24; N++) {
        Term *= Y2;
        Sum += Term / (double)(2 * N + 1);
    }
    return 2.0 * Sum;
}

double log10(double x) {
    const double Ln10 = 2.302585092994046;

    if (x <= 0.0) {
        return 0.0;
    }
    return log(x) / Ln10;
}
