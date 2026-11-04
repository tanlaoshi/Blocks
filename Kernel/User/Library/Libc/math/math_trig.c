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

static double sin_taylor(double radians) {
    double square = radians * radians;
    double term = radians;
    double sum = radians;
    int step;

    for (step = 1; step < 12; step++) {
        term *= -square / (double)((2 * step) * (2 * step + 1));
        sum += term;
    }
    return sum;
}

static double cos_taylor(double radians) {
    double square = radians * radians;
    double term = 1.0;
    double sum = 1.0;
    int step;

    for (step = 1; step < 12; step++) {
        term *= -square / (double)((2 * step - 1) * (2 * step));
        sum += term;
    }
    return sum;
}

/*
 * 把角收到 [-π, π]，再映到 [-π/2, π/2]，并用 Sign 记下 sin 的符号翻转。
 * CosSign：第二、三象限 cos 为负。
 */
static double reduce_for_sin_cos(double radians, int *sin_sign, int *cos_sign) {
    double angle = fmod(radians, MATH_TWO_PI);

    *sin_sign = 1;
    *cos_sign = 1;
    if (angle > MATH_PI) {
        angle -= MATH_TWO_PI;
    } else if (angle < -MATH_PI) {
        angle += MATH_TWO_PI;
    }
    if (angle > MATH_HALF_PI) {
        angle = MATH_PI - angle;
        *cos_sign = -1;
    } else if (angle < -MATH_HALF_PI) {
        angle = -MATH_PI - angle;
        *cos_sign = -1;
    }
    return angle;
}

double sin(double radians) {
    int sin_sign;
    int cos_sign;
    double angle = reduce_for_sin_cos(radians, &sin_sign, &cos_sign);

    (void)cos_sign;
    return (double)sin_sign * sin_taylor(angle);
}

double cos(double radians) {
    int sin_sign;
    int cos_sign;
    double angle = reduce_for_sin_cos(radians, &sin_sign, &cos_sign);

    (void)sin_sign;
    return (double)cos_sign * cos_taylor(angle);
}

double tan(double radians) {
    double cosine = cos(radians);
    double sine = sin(radians);

    if (fabs(cosine) < 1.0e-15) {
        return (sine >= 0.0) ? MATH_TAN_HUGE : -MATH_TAN_HUGE;
    }
    return sine / cosine;
}

double atan(double value) {
    double magnitude = fabs(value);
    double argument = magnitude;
    double square;
    double term;
    double sum;
    int step;
    int use_complement = 0;

    if (magnitude > 1.0) {
        argument = 1.0 / magnitude;
        use_complement = 1;
    }
    square = argument * argument;
    term = argument;
    sum = argument;
    for (step = 1; step < 16; step++) {
        term *= -square;
        sum += term / (double)(2 * step + 1);
    }
    if (use_complement) {
        sum = MATH_HALF_PI - sum;
    }
    return (value < 0.0) ? -sum : sum;
}

double atan2(double ordinate, double abscissa) {
    if (abscissa > 0.0) {
        return atan(ordinate / abscissa);
    }
    if (abscissa < 0.0 && ordinate >= 0.0) {
        return atan(ordinate / abscissa) + MATH_PI;
    }
    if (abscissa < 0.0 && ordinate < 0.0) {
        return atan(ordinate / abscissa) - MATH_PI;
    }
    if (abscissa == 0.0 && ordinate > 0.0) {
        return MATH_HALF_PI;
    }
    if (abscissa == 0.0 && ordinate < 0.0) {
        return -MATH_HALF_PI;
    }
    return 0.0;
}
