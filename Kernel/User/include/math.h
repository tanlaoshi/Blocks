/*
 * math.h — 教学软浮点子集（libc-math）
 */
#ifndef MATH_H
#define MATH_H

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

double fabs(double x);
double floor(double x);
double ceil(double x);
double sqrt(double x);
double pow(double x, double y);
double fmod(double x, double y);

double sin(double x);
double cos(double x);
double tan(double x);
double atan(double x);
double atan2(double y, double x);

double exp(double x);
double log(double x);
double log10(double x);

#endif
