#pragma once

#include "internal/constants.h"
#include "proj_lite.h"

#include <math.h>

static inline double adjlon(double lon)
{
    /* Let lon slightly overshoot, to avoid spurious sign switching at the date line */
    if (fabs(lon) < PI + 1e-12) return lon;

    /* adjust to 0..2pi range */
    lon += PI;

    /* remove integral # of 'revolutions'*/
    lon -= 2 * PI * floor(lon / (2 * PI));

    /* adjust back to -pi..pi range */
    lon -= PI;

    return lon;
}

static inline double pj_msfn(double sinphi, double cosphi, double es)
{
    return (cosphi / sqrt(1. - es * sinphi * sinphi));
}

static inline double pj_tsfn(double phi, double sinphi, double e)
{
    sinphi *= e;

    /* avoid zero division, fail gracefully */
    double denominator = 1.0 + sinphi;
    if (denominator == 0.0) return -9999;

    return (tan(.5 * (PI_OVER_2 - phi)) / pow((1. - sinphi) / denominator, .5 * e));
}

static inline double phi2z(double e, double ts)
{
    double e_over_2 = e / 2;
    double phi = PI_OVER_2 - 2 * atan(ts);

    for (int i = 0; i <= 15; ++i)
    {
        double con = e * sin(phi);
        double dphi = PI_OVER_2 - 2 * atan(ts * pow((1 - con) / (1 + con), e_over_2)) - phi;
        phi += dphi;

        if (fabs(dphi) <= 0.0000000001) return phi;
    }

    return -9999;
}

static inline double pl_hypot(double x, double y)
{
    x = fabs(x);
    y = fabs(y);
    if (x < y)
    {
        x /= y;
        return (y * sqrt(1. + x * x));
    }
    else
    {
        y /= (x != 0.0 ? x : 1.0);
        return (x * sqrt(1. + y * y));
    }
}

/* meridional distance for ellipsoid and inverse
 **  8th degree - accurate to < 1e-5 meters when used in conjunction
 **      with typical major axis values.
 **  Inverse determines phi to EPS (1e-11) radians, about 1e-6 seconds.
 */
#define C00 1.
#define C02 0.25
#define C04 0.046875
#define C06 0.01953125
#define C08 0.01068115234375
#define C22 0.75
#define C44 0.46875
#define C46 0.01302083333333333333
#define C48 0.00712076822916666666
#define C66 0.36458333333333333333
#define C68 0.00569661458333333333
#define C88 0.3076171875
#define EN_SIZE 5

static inline void pj_enfn(double es, double en[EN_SIZE])
{
    double t;
    en[0] = C00 - es * (C02 + es * (C04 + es * (C06 + es * C08)));
    en[1] = es * (C22 - es * (C04 + es * (C06 + es * C08)));
    en[2] = (t = es * es) * (C44 - es * (C46 + es * C48));
    en[3] = (t *= es) * (C66 - es * C68);
    en[4] = t * es * C88;
}

static inline double pj_mlfn(double phi, double sphi, double cphi, const double en[EN_SIZE])
{
    cphi *= sphi;
    sphi *= sphi;
    return (en[0] * phi - cphi * (en[1] + sphi * (en[2] + sphi * (en[3] + sphi * en[4]))));
}

static inline pl_Result pj_inv_mlfn(double arg, double es, const double en[EN_SIZE], double* phi)
{
    double s, t, k = 1. / (1. - es);

    *phi = arg;
    for (int i = 10; i; --i) /* rarely goes over 2 iterations */
    {
        s = sin(*phi);
        t = 1. - es * s * s;
        *phi -= t = (pj_mlfn(*phi, s, cos(*phi), en) - arg) * (t * sqrt(t)) * k;
        if (fabs(t) < 1e-11)
        {
            return pl_Result_Ok;
        }
    }

    return pl_Result_NonConvInvMeriDist;
}

void pl_geodetic_to_geocentric(
    double a,
    double b,
    int n,
    double* px,
    double* py,
    double* pz,
    int stride);

void pl_geocentric_to_geodetic(
    double a,
    double b,
    int n,
    double* px,
    double* py,
    double* pz,
    int stride);
