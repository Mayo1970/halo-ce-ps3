/* The same sin, pow and the rest on every port, bit for bit (system link simulates on every machine):
musl's, as halo_ calls the compiler cannot fold; with -ffp-contract=off, no fused multiply-adds. */

#ifndef __HALO_MATH_H
#define __HALO_MATH_H

double halo_sin(double x);
double halo_cos(double x);
double halo_tan(double x);
double halo_asin(double x);
double halo_acos(double x);
double halo_atan(double x);
double halo_atan2(double y, double x);
double halo_exp(double x);
double halo_log(double x);
double halo_log2(double x);
double halo_log10(double x);
double halo_pow(double x, double y);

#define sin(x) halo_sin(x)
#define cos(x) halo_cos(x)
#define tan(x) halo_tan(x)
#define asin(x) halo_asin(x)
#define acos(x) halo_acos(x)
#define atan(x) halo_atan(x)
#define atan2(y, x) halo_atan2(y, x)
#define exp(x) halo_exp(x)
#define log(x) halo_log(x)
/* (newlib's math.h has a log2 macro of its own) */
#undef log2
#define log2(x) halo_log2(x)
#define log10(x) halo_log10(x)
#define pow(x, y) halo_pow(x, y)

#endif
