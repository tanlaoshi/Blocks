/*
 * math_trig.c — sin / cos / tan / atan / atan2（教学软浮点）
 */
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static double ReducePi(double X) {
    const double TwoPi = 2.0 * M_PI;

    if (X > TwoPi || X < -TwoPi) {
        X = fmod(X, TwoPi);
    }
    while (X > M_PI) {
        X -= TwoPi;
    }
    while (X < -M_PI) {
        X += TwoPi;
    }
    return X;
}

double sin(double x) {
    double X = ReducePi(x);
    double X2 = X * X;
    double Term = X;
    double Sum = X;
    int N;

    /* Taylor：x - x^3/3! + x^5/5! - … */
    for (N = 1; N < 10; N++) {
        Term *= -X2 / (double)((2 * N) * (2 * N + 1));
        Sum += Term;
    }
    return Sum;
}

double cos(double x) {
    double X = ReducePi(x);
    double X2 = X * X;
    double Term = 1.0;
    double Sum = 1.0;
    int N;

    for (N = 1; N < 10; N++) {
        Term *= -X2 / (double)((2 * N - 1) * (2 * N));
        Sum += Term;
    }
    return Sum;
}

double tan(double x) {
    double C = cos(x);

    if (C == 0.0 || C == -0.0) {
        return 0.0;
    }
    return sin(x) / C;
}

double atan(double x) {
    double A;
    double A2;
    double Term;
    double Sum;
    int N;
    int Neg = 0;
    int Recip = 0;

    if (x < 0.0) {
        Neg = 1;
        x = -x;
    }
    if (x > 1.0) {
        Recip = 1;
        x = 1.0 / x;
    }
    A = x;
    A2 = A * A;
    Term = A;
    Sum = A;
    for (N = 1; N < 12; N++) {
        Term *= -A2;
        Sum += Term / (double)(2 * N + 1);
    }
    if (Recip) {
        Sum = M_PI / 2.0 - Sum;
    }
    if (Neg) {
        Sum = -Sum;
    }
    return Sum;
}

double atan2(double y, double x) {
    if (x > 0.0) {
        return atan(y / x);
    }
    if (x < 0.0 && y >= 0.0) {
        return atan(y / x) + M_PI;
    }
    if (x < 0.0 && y < 0.0) {
        return atan(y / x) - M_PI;
    }
    if (x == 0.0 && y > 0.0) {
        return M_PI / 2.0;
    }
    if (x == 0.0 && y < 0.0) {
        return -M_PI / 2.0;
    }
    return 0.0;
}
