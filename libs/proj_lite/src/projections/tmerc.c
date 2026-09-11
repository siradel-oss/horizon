// SPDX-FileCopyrightText: Copyright 2019 Siradel
// SPDX-License-Identifier: MIT

#include "internal/common.h"
#include "internal/projection.h"

#include <float.h>
#include <math.h>

typedef struct ParamsSph
{
    double es;
    double a;
    double x0;
    double y0;
    double k0;
    double lat0;
    double lon0;
    double esp;
    double ml0;
    double en[EN_SIZE];
} ParamsSph;

#define FC1 1.0
#define FC2 0.5
#define FC3 0.16666666666666666666
#define FC4 0.08333333333333333333
#define FC5 0.05
#define FC6 0.03333333333333333333
#define FC7 0.02380952380952380952
#define FC8 0.01785714285714285714
#define EPS10 1.e-10

static int tmerc_e_forward_approx(
    const pl_ProjectionDataOpaque* opq,
    int n,
    double* px,
    double* py,
    double* pz,
    int stride)
{
    PL_CHECK_PROJ_DATA_OPAQUE_SIZE(ParamsSph);
    const ParamsSph* Q = (const ParamsSph*)opq;
    int success = 0;

    for (int i = 0; i < n; ++i)
    {
        double lam = adjlon(*px - Q->lon0);
        double phi = *py;

        double al, als, n, cosphi, sinphi, t;

        /*
         * Fail if our longitude is more than 90 degrees from the
         * central meridian since the results are essentially garbage.
         * Is error -20 really an appropriate return value?
         *
         *  http://trac.osgeo.org/proj/ticket/5
         */
        if (lam < -PI_OVER_2 || lam > PI_OVER_2)
        {
            goto advance;
        }

        sinphi = sin(phi);
        cosphi = cos(phi);
        t = fabs(cosphi) > 1e-10 ? sinphi / cosphi : 0.;
        t *= t;
        al = cosphi * lam;
        als = al * al;
        al /= sqrt(1. - Q->es * sinphi * sinphi);
        n = Q->esp * cosphi * cosphi;
        *px = Q->k0 * al
            * (FC1
               + FC3 * als
                   * (1. - t + n
                      + FC5 * als
                          * (5. + t * (t - 18.) + n * (14. - 58. * t)
                             + FC7 * als * (61. + t * (t * (179. - t) - 479.)))));
        *py = Q->k0
            * (pj_mlfn(phi, sinphi, cosphi, Q->en) - Q->ml0
               + sinphi * al * lam * FC2
                   * (1.
                      + FC4 * als
                          * (5. - t + n * (9. + 4. * n)
                             + FC6 * als
                                 * (61. + t * (t - 58.) + n * (270. - 330 * t)
                                    + FC8 * als * (1385. + t * (t * (543. - t) - 3111.))))));

        *px = *px * Q->a + Q->x0;
        *py = *py * Q->a + Q->y0;

        success += 1;

    advance:
        px = (double*)((char*)px + stride);
        py = (double*)((char*)py + stride);
        pz = (double*)((char*)pz + stride);
    }

    return success;
}

static int tmerc_s_forward_approx(
    const pl_ProjectionDataOpaque* opq,
    int n,
    double* px,
    double* py,
    double* pz,
    int stride)
{
    PL_CHECK_PROJ_DATA_OPAQUE_SIZE(ParamsSph);
    const ParamsSph* Q = (const ParamsSph*)opq;
    int success = 0;

    for (int i = 0; i < n; ++i)
    {
        double lam = adjlon(*px - Q->lon0);
        double phi = *py;

        double b, cosphi;

        /*
         * Fail if our longitude is more than 90 degrees from the
         * central meridian since the results are essentially garbage.
         * Is error -20 really an appropriate return value?
         *
         *  http://trac.osgeo.org/proj/ticket/5
         */
        if (lam < -PI_OVER_2 || lam > PI_OVER_2)
        {
            goto advance;
        }

        cosphi = cos(phi);
        b = cosphi * sin(lam);
        if (fabs(fabs(b) - 1.) <= EPS10)
        {
            goto advance;
        }

        double x = Q->ml0 * log((1. + b) / (1. - b));
        double y = cosphi * cos(lam) / sqrt(1. - b * b);

        b = fabs(y);
        if (b >= 1.)
        {
            if ((b - 1.) > EPS10)
            {
                goto advance;
            }
            else
            {
                y = 0.;
            }
        }
        else
        {
            y = acos(y);
        }

        if (phi < 0.)
        {
            y = -y;
        }

        y = Q->esp * (y - Q->lat0);

        *px = x * Q->a + Q->x0;
        *py = y * Q->a + Q->y0;

        success += 1;

    advance:
        px = (double*)((char*)px + stride);
        py = (double*)((char*)py + stride);
        pz = (double*)((char*)pz + stride);
    }

    return success;
}

static int tmerc_e_inverse_approx(
    const pl_ProjectionDataOpaque* opq,
    int n,
    double* px,
    double* py,
    double* pz,
    int stride)
{
    PL_CHECK_PROJ_DATA_OPAQUE_SIZE(ParamsSph);
    const ParamsSph* Q = (const ParamsSph*)opq;
    int success = 0;
    double inv_a = 1 / Q->a;

    for (int i = 0; i < n; ++i)
    {
        double x = (*px - Q->x0) * inv_a;
        double y = (*py - Q->y0) * inv_a;

        double n, con, cosphi, d, ds, sinphi, t;

        double lam;
        double phi;
        pj_inv_mlfn(Q->ml0 + y / Q->k0, Q->es, Q->en, &phi);

        if (fabs(phi) >= PI_OVER_2)
        {
            phi = y < 0. ? -PI_OVER_2 : PI_OVER_2;
            lam = 0.;
        }
        else
        {
            sinphi = sin(phi);
            cosphi = cos(phi);
            t = fabs(cosphi) > 1e-10 ? sinphi / cosphi : 0.;
            n = Q->esp * cosphi * cosphi;
            d = x * sqrt(con = 1. - Q->es * sinphi * sinphi) / Q->k0;
            con *= t;
            t *= t;
            ds = d * d;
            phi -= (con * ds / (1. - Q->es)) * FC2
                * (1.
                   - ds * FC4
                       * (5. + t * (3. - 9. * n) + n * (1. - 4 * n)
                          - ds * FC6
                              * (61. + t * (90. - 252. * n + 45. * t) + 46. * n
                                 - ds * FC8 * (1385. + t * (3633. + t * (4095. + 1575. * t))))));
            lam = d
                * (FC1
                   - ds * FC3
                       * (1. + 2. * t + n
                          - ds * FC5
                              * (5. + t * (28. + 24. * t + 8. * n) + 6. * n
                                 - ds * FC7 * (61. + t * (662. + t * (1320. + 720. * t))))))
                / cosphi;
        }

        *px = adjlon(Q->lon0 + lam);
        *py = phi;
        success += 1;

        px = (double*)((char*)px + stride);
        py = (double*)((char*)py + stride);
        pz = (double*)((char*)pz + stride);
    }

    return success;
}

static int tmerc_s_inverse_approx(
    const pl_ProjectionDataOpaque* opq,
    int n,
    double* px,
    double* py,
    double* pz,
    int stride)
{
    PL_CHECK_PROJ_DATA_OPAQUE_SIZE(ParamsSph);
    const ParamsSph* Q = (const ParamsSph*)opq;
    int success = 0;
    double inv_a = 1 / Q->a;

    for (int i = 0; i < n; ++i)
    {
        double x = (*px - Q->x0) * inv_a;
        double y = (*py - Q->y0) * inv_a;

        double h, g;

        h = exp(x / Q->esp);
        if (h == 0)
        {
            goto advance;
        }

        g = .5 * (h - 1. / h);
        h = cos(Q->lat0 + y / Q->esp);

        double phi = asin(sqrt((1. - h * h) / (1. + g * g)));

        /* Make sure that phi is on the correct hemisphere when false northing is used */
        if (y < 0. && -phi + Q->lat0 < 0.0) phi = -phi;

        double lam = (g != 0.0 || h != 0.0) ? atan2(g, h) : 0.;

        *px = adjlon(Q->lon0 + lam);
        *py = phi;
        success += 1;

    advance:
        px = (double*)((char*)px + stride);
        py = (double*)((char*)py + stride);
        pz = (double*)((char*)pz + stride);
    }

    return success;
}

static pl_Result bake_params_approx(const pl_Crs* crs, ParamsSph* Q)
{
    Q->a = crs->a;
    Q->x0 = crs->params.x_0;
    Q->y0 = crs->params.y_0;
    Q->lat0 = crs->params.lat_0;
    Q->lon0 = crs->params.lon_0;
    Q->es = 1.0 - (crs->b * crs->b) / (crs->a * crs->a);
    Q->k0 = crs->params.k_0;

    if (crs->a != crs->b)
    {
        pj_enfn(Q->es, Q->en);
        Q->ml0 = pj_mlfn(crs->params.lat_0, sin(crs->params.lat_0), cos(crs->params.lat_0), Q->en);
        Q->esp = Q->es / (1 - Q->es);
    }
    else
    {
        Q->esp = crs->params.k_0;
        Q->ml0 = Q->esp / 2;
    }

    return pl_Result_Ok;
}

PL_BAKE_FWD_PROJ_FN(tmerc)
{
    // In theory tmerc could use the non approximative
    // versions, but proj.4 uses the approximation for
    // everyting, so unless we don't compare against it, we're
    // stuck with it.

    PL_CHECK_PROJ_DATA_OPAQUE_SIZE(ParamsSph);
    ParamsSph* params = (ParamsSph*)opq;

    pl_Result res = bake_params_approx(crs, params);
    if (res != pl_Result_Ok) return res;

    if (crs->a == crs->b)
    {
        *fn = tmerc_s_forward_approx;
    }
    else
    {
        *fn = tmerc_e_forward_approx;
    }

    return pl_Result_Ok;
}

PL_BAKE_INV_PROJ_FN(tmerc)
{
    PL_CHECK_PROJ_DATA_OPAQUE_SIZE(ParamsSph);
    ParamsSph* params = (ParamsSph*)opq;

    pl_Result res = bake_params_approx(crs, params);
    if (res != pl_Result_Ok) return res;

    if (crs->a == crs->b)
    {
        *fn = tmerc_s_inverse_approx;
    }
    else
    {
        *fn = tmerc_e_inverse_approx;
    }

    return pl_Result_Ok;
}
