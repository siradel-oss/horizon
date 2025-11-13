#include "internal/common.h"
#include "internal/projection.h"

#include <float.h>
#include <math.h>

typedef struct Params
{
    double k0;
    double a;
    double b;
    double e;
    double x0;
    double y0;
    double lat0;
    double lon0;
} Params;

PL_REG_PROJ_PARAM_FN(merc)
{
    if (pl_ss_compare(&param, "lat_ts"))
    {
        p->merc.has_lat_ts = true;
        p->merc.lat_ts = pl_ss_to_double(&value) * DEG2RAD;
        return true;
    }

    return false;
}

#define EPS10 1.e-10

static double logtanpfpim1(double x)
{
    if (fabs(x) <= DBL_EPSILON)
    {
        /* tan(M_FORTPI + .5 * x) can be approximated by  1.0 + x */
        return log1p(x);
    }
    return log(tan(PI_OVER_4 + .5 * x));
}

static int merc_e_forward(
    const pl_ProjectionDataOpaque* opq,
    int n,
    double* px,
    double* py,
    double* pz,
    int stride)
{
    (void)pz;

    PL_CHECK_PROJ_DATA_OPAQUE_SIZE(Params);
    const Params* params = (const Params*)opq;
    int success = 0;

    for (int i = 0; i < n; ++i)
    {
        double lon = adjlon(*px - params->lon0);
        double lat = *py;

        if (fabs(fabs(lat) - PI_OVER_2) <= EPS10)
        {
            goto advance;
        }

        *px = params->x0 + params->a * params->k0 * lon;
        *py = params->y0 - params->a * params->k0 * log(pj_tsfn(lat, sin(lat), params->e));
        success += 1;

    advance:
        px = (double*)((char*)px + stride);
        py = (double*)((char*)py + stride);
    }

    return success;
}

static int merc_s_forward(
    const pl_ProjectionDataOpaque* opq,
    int n,
    double* px,
    double* py,
    double* pz,
    int stride)
{
    (void)pz;

    PL_CHECK_PROJ_DATA_OPAQUE_SIZE(Params);
    const Params* params = (const Params*)opq;
    int success = 0;

    for (int i = 0; i < n; ++i)
    {
        double lon = adjlon(*px - params->lon0);
        double lat = *py;

        if (fabs(fabs(lat) - PI_OVER_2) <= EPS10)
        {
            goto advance;
        }

        *px = params->x0 + params->a * params->k0 * lon;
        *py = params->y0 + params->a * params->k0 * logtanpfpim1(lat);
        success += 1;

    advance:
        px = (double*)((char*)px + stride);
        py = (double*)((char*)py + stride);
    }

    return success;
}

static int merc_e_inverse(
    const pl_ProjectionDataOpaque* opq,
    int n,
    double* px,
    double* py,
    double* pz,
    int stride)
{
    (void)pz;

    PL_CHECK_PROJ_DATA_OPAQUE_SIZE(Params);
    const Params* params = (const Params*)opq;
    int success = 0;

    double scale = 1.0 / (params->a * params->k0);

    for (int i = 0; i < n; ++i)
    {
        double x = *px - params->x0;
        double y = *py - params->y0;

        double ts = exp(-y * scale);
        double lat = phi2z(params->e, ts);

        if (lat == -9999)
        {
            goto advance;
        }

        *px = adjlon(x * scale + params->lon0);
        *py = lat;
        success += 1;

    advance:
        px = (double*)((char*)px + stride);
        py = (double*)((char*)py + stride);
    }

    return success;
}

static int merc_s_inverse(
    const pl_ProjectionDataOpaque* opq,
    int n,
    double* px,
    double* py,
    double* pz,
    int stride)
{
    (void)pz;

    PL_CHECK_PROJ_DATA_OPAQUE_SIZE(Params);
    const Params* params = (const Params*)opq;
    int success = 0;

    double scale = 1.0 / (params->a * params->k0);

    for (int i = 0; i < n; ++i)
    {
        double x = *px - params->x0;
        double y = *py - params->y0;

        *px = adjlon(x * scale + params->lon0);
        *py = PI_OVER_2 - 2 * atan(exp(-y * scale));
        success += 1;

        px = (double*)((char*)px + stride);
        py = (double*)((char*)py + stride);
    }

    return success;
}

static pl_Result bake_params_common(const pl_Crs* crs, Params* params)
{
    double phits = fabs(crs->params.merc.lat_ts);
    if (phits >= PI_OVER_2)
    {
        return pl_Result_LatTsLargerThan90;
    }

    double es = 1.0 - (crs->b * crs->b) / (crs->a * crs->a);

    params->a = crs->a;
    params->b = crs->b;
    params->x0 = crs->params.x_0;
    params->y0 = crs->params.y_0;
    params->lat0 = crs->params.lat_0;
    params->lon0 = crs->params.lon_0;
    params->k0 = crs->params.k_0;
    params->e = sqrt(es);

    if (crs->a != crs->b)
    {
        if (crs->params.merc.has_lat_ts)
        {
            params->k0 = pj_msfn(sin(phits), cos(phits), es);
        }
    }
    else
    {
        if (crs->params.merc.has_lat_ts)
        {
            params->k0 = cos(phits);
        }
    }

    return pl_Result_Ok;
}

PL_BAKE_FWD_PROJ_FN(merc)
{
    PL_CHECK_PROJ_DATA_OPAQUE_SIZE(Params);
    Params* params = (Params*)opq;

    pl_Result res = bake_params_common(crs, params);
    if (res != pl_Result_Ok) return res;

    if (crs->a == crs->b)
    {
        *fn = merc_s_forward;
    }
    else
    {
        *fn = merc_e_forward;
    }

    return pl_Result_Ok;
}

PL_BAKE_INV_PROJ_FN(merc)
{
    PL_CHECK_PROJ_DATA_OPAQUE_SIZE(Params);
    Params* params = (Params*)opq;

    pl_Result res = bake_params_common(crs, params);
    if (res != pl_Result_Ok) return res;

    if (crs->a == crs->b)
    {
        *fn = merc_s_inverse;
    }
    else
    {
        *fn = merc_e_inverse;
    }

    return pl_Result_Ok;
}
