/*
 * math.c — 教学软浮点子集（libc-math）
 */
#include <math.h>

double fabs(double x) {
    return x < 0.0 ? -x : x;
}

double floor(double x) {
    long N;

    if (x >= 0.0) {
        N = (long)x;
        return (double)N;
    }
    N = (long)x;
    if ((double)N == x) {
        return x;
    }
    return (double)(N - 1);
}

double ceil(double x) {
    long N;

    if (x <= 0.0) {
        N = (long)x;
        return (double)N;
    }
    N = (long)x;
    if ((double)N == x) {
        return x;
    }
    return (double)(N + 1);
}

double fmod(double x, double y) {
    long Q;

    if (y == 0.0) {
        return 0.0;
    }
    Q = (long)(x / y);
    return x - (double)Q * y;
}

double sqrt(double x) {
    double G;
    int I;

    if (x < 0.0) {
        return 0.0;
    }
    if (x == 0.0) {
        return 0.0;
    }
    G = x;
    if (G < 1.0) {
        G = 1.0;
    }
    for (I = 0; I < 32; I++) {
        G = 0.5 * (G + x / G);
    }
    return G;
}

double pow(double x, double y) {
    int Exp;
    int I;
    double Acc;

    if (y == 0.0) {
        return 1.0;
    }
    Exp = (int)y;
    if ((double)Exp != y || Exp < 0) {
        /* 非整数指数：教学简化，只支持整数 */
        return 0.0;
    }
    Acc = 1.0;
    for (I = 0; I < Exp; I++) {
        Acc *= x;
    }
    return Acc;
}
