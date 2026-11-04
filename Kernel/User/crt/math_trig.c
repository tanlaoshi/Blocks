/*
 * math_trig.c — sin / cos / tan / atan / atan2（教学软浮点）
 *
 * 角归约到 [-π/2, π/2] 再 Taylor，避免 sin(π) 在 π 附近级数失真。
 */
#include <math.h>

#define MATH_PI 3.14159265358979323846
#define MATH_HALF_PI (MATH_PI / 2.0)
#define MATH_TWO_PI (MATH_PI * 2.0)
#define MATH_TAN_HUGE 1.0e300

static double SinTaylor(double Radians) {
    double Square = Radians * Radians;
    double Term = Radians;
    double Sum = Radians;
    int Step;

    for (Step = 1; Step < 12; Step++) {
        Term *= -Square / (double)((2 * Step) * (2 * Step + 1));
        Sum += Term;
    }
    return Sum;
}

static double CosTaylor(double Radians) {
    double Square = Radians * Radians;
    double Term = 1.0;
    double Sum = 1.0;
    int Step;

    for (Step = 1; Step < 12; Step++) {
        Term *= -Square / (double)((2 * Step - 1) * (2 * Step));
        Sum += Term;
    }
    return Sum;
}

/*
 * 把角收到 [-π, π]，再映到 [-π/2, π/2]，并用 Sign 记下 sin 的符号翻转。
 * CosSign：第二、三象限 cos 为负。
 */
static double ReduceForSinCos(double Radians, int *SinSign, int *CosSign) {
    double Angle = fmod(Radians, MATH_TWO_PI);

    *SinSign = 1;
    *CosSign = 1;
    if (Angle > MATH_PI) {
        Angle -= MATH_TWO_PI;
    } else if (Angle < -MATH_PI) {
        Angle += MATH_TWO_PI;
    }
    if (Angle > MATH_HALF_PI) {
        Angle = MATH_PI - Angle;
        *CosSign = -1;
    } else if (Angle < -MATH_HALF_PI) {
        Angle = -MATH_PI - Angle;
        *CosSign = -1;
    }
    return Angle;
}

double sin(double Radians) {
    int SinSign;
    int CosSign;
    double Angle = ReduceForSinCos(Radians, &SinSign, &CosSign);

    (void)CosSign;
    return (double)SinSign * SinTaylor(Angle);
}

double cos(double Radians) {
    int SinSign;
    int CosSign;
    double Angle = ReduceForSinCos(Radians, &SinSign, &CosSign);

    (void)SinSign;
    return (double)CosSign * CosTaylor(Angle);
}

double tan(double Radians) {
    double Cosine = cos(Radians);
    double Sine = sin(Radians);

    if (fabs(Cosine) < 1.0e-15) {
        return (Sine >= 0.0) ? MATH_TAN_HUGE : -MATH_TAN_HUGE;
    }
    return Sine / Cosine;
}

double atan(double Value) {
    double Magnitude = fabs(Value);
    double Argument = Magnitude;
    double Square;
    double Term;
    double Sum;
    int Step;
    int UseComplement = 0;

    if (Magnitude > 1.0) {
        Argument = 1.0 / Magnitude;
        UseComplement = 1;
    }
    Square = Argument * Argument;
    Term = Argument;
    Sum = Argument;
    for (Step = 1; Step < 16; Step++) {
        Term *= -Square;
        Sum += Term / (double)(2 * Step + 1);
    }
    if (UseComplement) {
        Sum = MATH_HALF_PI - Sum;
    }
    return (Value < 0.0) ? -Sum : Sum;
}

double atan2(double Ordinate, double Abscissa) {
    if (Abscissa > 0.0) {
        return atan(Ordinate / Abscissa);
    }
    if (Abscissa < 0.0 && Ordinate >= 0.0) {
        return atan(Ordinate / Abscissa) + MATH_PI;
    }
    if (Abscissa < 0.0 && Ordinate < 0.0) {
        return atan(Ordinate / Abscissa) - MATH_PI;
    }
    if (Abscissa == 0.0 && Ordinate > 0.0) {
        return MATH_HALF_PI;
    }
    if (Abscissa == 0.0 && Ordinate < 0.0) {
        return -MATH_HALF_PI;
    }
    return 0.0;
}
