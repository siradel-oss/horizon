#include "proj_lite_internal/common.h"
#include "proj_lite_internal/projection.h"

#include <float.h>
#include <math.h>

#define EPS10 1e-10

PL_REG_PROJ_PARAM_FN(lcc)
{
    if (pl_ss_compare(&param, "lat_0"))
    {
        p->lcc.has_lat_0 = true;
        return true;
    }
    else if (pl_ss_compare(&param, "lat_1"))
    {
        p->lcc.lat_1 = pl_ss_to_double(&value) * DEG2RAD;
        return true;
    }
    else if (pl_ss_compare(&param, "lat_2"))
    {
        p->lcc.lat_2 = pl_ss_to_double(&value) * DEG2RAD;
        p->lcc.has_lat_2 = true;
        return true;
    }

    return false;
}

typedef struct Params
{
    double a;
    double x0;
    double y0;
    double k0;
    double lon0;
    double phi0;
    double phi1;
    double phi2;
    double n;
    double rho0;
    double c;
    double es;
    double e;
} Params;

static int lcc_e_forward(
    const pl_ProjectionDataOpaque* opq,
    int n,
    double* px,
    double* py,
    double* pz,
    int stride)
{
    PL_CHECK_PROJ_DATA_OPAQUE_SIZE(Params);
    const Params* Q = (const Params*)opq;
    int success = 0;

    // Hopefully it will help branch prediction
    const bool is_sphere = Q->es == 0.0;

    for (int i = 0; i < n; ++i)
    {
        double phi = *py;
        double lam = adjlon(*px - Q->lon0);

        double rho;

        if (fabs(fabs(phi) - PI_OVER_2) < EPS10)
        {
            if ((phi * Q->n) <= 0.)
            {
                goto advance;
            }

            rho = 0.;
        }
        else
        {
            rho = Q->c
                * ((!is_sphere) ? pow(pj_tsfn(phi, sin(phi), Q->e), Q->n)
                                : pow(tan(PI_OVER_4 + .5 * phi), -Q->n));
        }

        lam *= Q->n;

        double x = Q->k0 * (rho * sin(lam));
        double y = Q->k0 * (Q->rho0 - rho * cos(lam));

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

static int lcc_e_inverse(
    const pl_ProjectionDataOpaque* opq,
    int n,
    double* px,
    double* py,
    double* pz,
    int stride)
{
    PL_CHECK_PROJ_DATA_OPAQUE_SIZE(Params);
    const Params* Q = (const Params*)opq;
    int success = 0;
    double proj_scale = 1.0 / (Q->a * Q->k0);

    // Hopefully it will help branch prediction
    const bool is_sphere = Q->es == 0.0;

    for (int i = 0; i < n; ++i)
    {
        double x = (*px - Q->x0) * proj_scale;
        double y = (*py - Q->y0) * proj_scale;
        double rho;

        y = Q->rho0 - y;
        rho = hypot(x, y);

        double phi;
        double lam;

        if (rho != 0.)
        {
            if (Q->n < 0.)
            {
                rho = -rho;
                x = -x;
                y = -y;
            }

            if (!is_sphere)
            {
                phi = phi2z(Q->e, pow(rho / Q->c, 1. / Q->n));
                if (phi == -9999)
                {
                    goto advance;
                }
            }
            else
            {
                phi = 2. * atan(pow(Q->c / rho, 1. / Q->n)) - PI_OVER_2;
            }

            lam = atan2(x, y) / Q->n;
        }
        else
        {
            lam = 0.;
            phi = Q->n > 0. ? PI_OVER_2 : -PI_OVER_2;
        }

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

static pl_Result bake_params(const pl_Crs* crs, Params* Q)
{
    Q->a = crs->a;
    Q->x0 = crs->params.x_0;
    Q->y0 = crs->params.y_0;
    Q->phi0 = crs->params.lat_0;
    Q->lon0 = crs->params.lon_0;
    Q->k0 = crs->params.k_0;

    Q->phi1 = crs->params.lcc.lat_1;
    if (crs->params.lcc.has_lat_2)
    {
        Q->phi2 = crs->params.lcc.lat_2;
    }
    else
    {
        Q->phi2 = Q->phi1;
        if (!crs->params.lcc.has_lat_0)
        {
            Q->phi0 = Q->phi1;
        }
    }

    if (fabs(Q->phi1) > PI_OVER_2 || fabs(Q->phi2) > PI_OVER_2)
    {
        return pl_Result_LatitudeOutOfRange;
    }

    if (fabs(Q->phi1 + Q->phi2) < EPS10)
    {
        return pl_Result_ConicLatEqual;
    }

    double sinphi = sin(Q->phi1);
    Q->n = sinphi;

    double cosphi = cos(Q->phi1);
    bool secant = fabs(Q->phi1 - Q->phi2) >= EPS10;

    double es = 1 - (crs->b * crs->b) / (crs->a * crs->a);
    double e = sqrt(es);

    Q->es = es;
    Q->e = e;

    if (es != 0)
    {
        double ml1, m1;

        m1 = pj_msfn(sinphi, cosphi, es);
        ml1 = pj_tsfn(Q->phi1, sinphi, e);

        if (ml1 == 0)
        {
            return pl_Result_Lat1Or2ZeroOr90;
        }

        if (secant) /* secant cone */
        {
            sinphi = sin(Q->phi2);

            Q->n = log(m1 / pj_msfn(sinphi, cos(Q->phi2), es));

            if (Q->n == 0)
            {
                // Not quite, but es is very close to 1...
                return pl_Result_InvalidEccentricity;
            }

            const double ml2 = pj_tsfn(Q->phi2, sinphi, e);
            if (ml2 == 0)
            {
                return pl_Result_Lat1Or2ZeroOr90;
            }

            const double denom = log(ml1 / ml2);
            if (denom == 0)
            {
                // Not quite, but es is very close to 1...
                return pl_Result_InvalidEccentricity;
            }

            Q->n /= denom;
        }

        Q->c = (Q->rho0 = m1 * pow(ml1, -Q->n) / Q->n);
        Q->rho0 *= (fabs(fabs(Q->phi0) - PI_OVER_2) < EPS10)
            ? 0.
            : pow(pj_tsfn(Q->phi0, sin(Q->phi0), e), Q->n);
    }
    else
    {
        if (fabs(cosphi) < EPS10 || fabs(cos(Q->phi2)) < EPS10)
        {
            return pl_Result_Lat1Or2ZeroOr90;
        }

        if (secant)
        {
            Q->n = log(cosphi / cos(Q->phi2))
                / log(tan(PI_OVER_4 + .5 * Q->phi2) / tan(PI_OVER_4 + .5 * Q->phi1));
        }

        Q->c = cosphi * pow(tan(PI_OVER_4 + .5 * Q->phi1), Q->n) / Q->n;
        Q->rho0 = (fabs(fabs(Q->phi0) - PI_OVER_2) < EPS10)
            ? 0.
            : Q->c * pow(tan(PI_OVER_4 + .5 * Q->phi0), -Q->n);
    }

    return pl_Result_Ok;
}

PL_BAKE_FWD_PROJ_FN(lcc)
{
    PL_CHECK_PROJ_DATA_OPAQUE_SIZE(Params);
    Params* params = (Params*)opq;

    pl_Result res = bake_params(crs, params);
    if (res != pl_Result_Ok) return res;

    *fn = lcc_e_forward;

    return pl_Result_Ok;
}

PL_BAKE_INV_PROJ_FN(lcc)
{
    PL_CHECK_PROJ_DATA_OPAQUE_SIZE(Params);
    Params* params = (Params*)opq;

    pl_Result res = bake_params(crs, params);
    if (res != pl_Result_Ok) return res;

    *fn = lcc_e_inverse;

    return pl_Result_Ok;
}
