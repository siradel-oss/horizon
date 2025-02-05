#include "proj_lite_internal/common.h"
#include "proj_lite_internal/projection.h"

#include <float.h>
#include <math.h>

void pl_geodetic_to_geocentric(
    double a,
    double b,
    int n,
    double* px,
    double* py,
    double* pz,
    int stride)
{
    const double e2 = 1.0 - (b * b) / (a * a);

    for (int i = 0; i < n; ++i)
    {
        double lon = *px;
        double lat = *py;
        double alt = *pz;

        if (lat < -PI_OVER_2 && lat > -1.001 * PI_OVER_2)
        {
            lat = -PI_OVER_2;
        }
        else if (lat > PI_OVER_2 && lat < 1.001 * PI_OVER_2)
        {
            lat = PI_OVER_2;
        }
        else if ((lat < -PI_OVER_2) || (lat > PI_OVER_2))
        {
            goto advance;
        }

        while (lon > PI)
        {
            lon -= 2 * PI;
        }

        double sin_lat = sin(lat);
        double cos_lat = cos(lat);
        double sin2_lat = sin_lat * sin_lat;

        double rn = a / (sqrt(1 - e2 * sin2_lat));
        *px = (rn + alt) * cos_lat * cos(lon);
        *py = (rn + alt) * cos_lat * sin(lon);
        *pz = ((rn * (1 - e2)) + alt) * sin_lat;

    advance:
        px = (double*)((char*)px + stride);
        py = (double*)((char*)py + stride);
        pz = (double*)((char*)pz + stride);
    }
}

static double geocentric_radius(double a, double b, double cphi, double sphi)
{
    return pl_hypot(a * a * cphi, b * b * sphi) / pl_hypot(a * cphi, b * sphi);
}

void pl_geocentric_to_geodetic(
    double a,
    double b,
    int n,
    double* px,
    double* py,
    double* pz,
    int stride)
{
    const double e2s = (a * a) / (b * b) - 1;
    const double es = 1 - (b * b) / (a * a);

    for (int i = 0; i < n; ++i)
    {
        double x = *px;
        double y = *py;
        double z = *pz;

        double p = pl_hypot(x, y);
        double theta = atan2(z * a, p * b);

        double c = cos(theta);
        double s = sin(theta);

        double phi = atan2(z + e2s * b * s * s * s, p - es * a * c * c * c);
        if (fabs(phi) > PI_OVER_2)
        {
            // this happen on non-sphere ellipsoid when x,y,z is very close to 0
            // there is no single solution to the cart->geodetic conversion in
            // that case, so arbitrarily pickup phi = 0.
            phi = 0;
        }

        double lam = atan2(y, x);
        double sinphi = sin(phi);
        double N = a / sqrt(1 - es * sinphi * sinphi);

        c = cos(phi);

        double alt;
        if (fabs(c) < 1e-6)
        {
            /* poleward of 89.99994 deg, we avoid division by zero   */
            /* by computing the height as the cartesian z value      */
            /* minus the geocentric radius of the Earth at the given */
            /* latitude                                              */
            double r = geocentric_radius(a, b, c, sinphi);
            alt = fabs(z) - r;
        }
        else
        {
            alt = p / c - N;
        }

        *px = lam;
        *py = phi;
        *pz = alt;

        px = (double*)((char*)px + stride);
        py = (double*)((char*)py + stride);
        pz = (double*)((char*)pz + stride);
    }
}

typedef struct Params
{
    double a;
    double b;
} Params;

static int geocent_forward(
    const pl_ProjectionDataOpaque* opq,
    int n,
    double* px,
    double* py,
    double* pz,
    int stride)
{
    PL_CHECK_PROJ_DATA_OPAQUE_SIZE(Params);
    const Params* Q = (const Params*)opq;

    pl_geodetic_to_geocentric(Q->a, Q->b, n, px, py, pz, stride);

    return n;
}

static int geocent_inverse(
    const pl_ProjectionDataOpaque* opq,
    int n,
    double* px,
    double* py,
    double* pz,
    int stride)
{
    PL_CHECK_PROJ_DATA_OPAQUE_SIZE(Params);
    const Params* Q = (const Params*)opq;

    pl_geocentric_to_geodetic(Q->a, Q->b, n, px, py, pz, stride);

    return n;
}

static pl_Result bake_params(const pl_Crs* crs, Params* Q)
{
    Q->a = crs->a;
    Q->b = crs->b;
    return pl_Result_Ok;
}

PL_BAKE_FWD_PROJ_FN(geocent)
{
    PL_CHECK_PROJ_DATA_OPAQUE_SIZE(Params);
    Params* params = (Params*)opq;

    pl_Result res = bake_params(crs, params);
    if (res != pl_Result_Ok) return res;

    *fn = geocent_forward;

    return pl_Result_Ok;
}

PL_BAKE_INV_PROJ_FN(geocent)
{
    PL_CHECK_PROJ_DATA_OPAQUE_SIZE(Params);
    Params* params = (Params*)opq;

    pl_Result res = bake_params(crs, params);
    if (res != pl_Result_Ok) return res;

    *fn = geocent_inverse;

    return pl_Result_Ok;
}
