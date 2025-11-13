#include "internal/common.h"
#include "internal/projection.h"

#include <float.h>
#include <math.h>

#define PROJ_ETMERC_ORDER 6

static pl_Result bake_parameters_from_utm_zone(pl_ProjectionParams* p)
{
    p->y_0 = p->utm.south ? 10000000. : 0.;
    p->x_0 = 500000.;

    int zone = p->utm.zone;
    if (zone > 0 && zone <= 60)
    {
        zone -= 1;
    }
    else
    {
        return pl_Result_InvalidUtmZone;
    }

    p->lon_0 = (zone + .5) * PI / 30. - PI;
    p->k_0 = 0.9996;
    p->lat_0 = 0.;

    return pl_Result_Ok;
}

PL_REG_PROJ_PARAM_FN(utm)
{
    bool ret_value = false;

    if (pl_ss_compare(&param, "south"))
    {
        p->utm.south = true;
        ret_value = true;
    }
    else if (pl_ss_compare(&param, "zone"))
    {
        p->utm.zone = pl_ss_to_int(&value);
        p->utm.has_zone = true;
        ret_value = true;
    }

    if (ret_value && p->utm.has_zone)
    {
        pl_Result res = bake_parameters_from_utm_zone(p);
        if (res != pl_Result_Ok) ret_value = false;
    }

    return ret_value;
}

typedef struct ParamsFwd
{
    double projected_scale;
    double x0;
    double y0;
    double lat0;
    double lon0;
    double cbg[PROJ_ETMERC_ORDER];
    double gtu[PROJ_ETMERC_ORDER];
    double Qn;
    double Zb;
} ParamsFwd;

typedef struct ParamsInv
{
    double projected_scale;
    double x0;
    double y0;
    double lat0;
    double lon0;
    double cgb[PROJ_ETMERC_ORDER];
    double utg[PROJ_ETMERC_ORDER];
    double Qn;
    double Zb;
} ParamsInv;

static double clenS(const double* a, int size, double arg_r, double arg_i, double* R, double* I)
{
    const double* p;
    double r, i, hr, hr1, hr2, hi, hi1, hi2;
    double sin_arg_r, cos_arg_r, sinh_arg_i, cosh_arg_i;

    /* arguments */
    p = a + size;
    sin_arg_r = sin(arg_r);
    cos_arg_r = cos(arg_r);
    sinh_arg_i = sinh(arg_i);
    cosh_arg_i = cosh(arg_i);
    r = 2 * cos_arg_r * cosh_arg_i;
    i = -2 * sin_arg_r * sinh_arg_i;

    /* summation loop */
    hi1 = hr1 = hi = 0;
    hr = *--p;
    while (a - p)
    {
        hr2 = hr1;
        hi2 = hi1;
        hr1 = hr;
        hi1 = hi;
        hr = -hr2 + r * hr1 - i * hi1 + *--p;
        hi = -hi2 + i * hr1 + r * hi1;
    }

    r = sin_arg_r * cosh_arg_i;
    i = cos_arg_r * sinh_arg_i;
    *R = r * hr - i * hi;
    *I = r * hi + i * hr;
    return *R;
}

static double clens(const double* a, int size, double arg_r)
{
    const double* p;
    double r, hr, hr1, hr2, cos_arg_r;

    p = a + size;
    cos_arg_r = cos(arg_r);
    r = 2 * cos_arg_r;

    /* summation loop */
    hr1 = 0;
    hr = *--p;
    for (; a - p;)
    {
        hr2 = hr1;
        hr1 = hr;
        hr = -hr2 + r * hr1 + *--p;
    }
    return sin(arg_r) * hr;
}

static double gatg(const double* p1, int len_p1, double B)
{
    const double* p;
    double h = 0, h1, h2 = 0, cos_2B;

    cos_2B = 2 * cos(2 * B);
    p = p1 + len_p1;
    h1 = *--p;
    while (p - p1)
    {
        h = -h2 + cos_2B * h1 + *--p;
        h2 = h1;
        h1 = h;
    }
    return (B + h * sin(2 * B));
}

static int tmerc_e_inverse(
    const pl_ProjectionDataOpaque* opq,
    int n,
    double* px,
    double* py,
    double* pz,
    int stride)
{
    PL_CHECK_PROJ_DATA_OPAQUE_SIZE(ParamsInv);
    const ParamsInv* Q = (const ParamsInv*)opq;
    int success = 0;

    for (int i = 0; i < n; ++i)
    {
        double x = (*px - Q->x0) * Q->projected_scale;
        double y = (*py - Q->y0) * Q->projected_scale;

        double sin_Cn, cos_Cn, cos_Ce, sin_Ce, dCn, dCe;
        double Cn = y, Ce = x;

        /* normalize N, E */
        Cn = (Cn - Q->Zb) / Q->Qn;
        Ce = Ce / Q->Qn;

        if (fabs(Ce) <= 2.623395162778) /* 150 degrees */
        {
            /* norm. N, E -> compl. sph. LAT, LNG */
            Cn += clenS(Q->utg, PROJ_ETMERC_ORDER, 2 * Cn, 2 * Ce, &dCn, &dCe);
            Ce += dCe;
            Ce = atan(sinh(Ce)); /* Replaces: Ce = 2*(atan(exp(Ce)) - FORTPI); */

            /* compl. sph. LAT -> Gaussian LAT, LNG */
            sin_Cn = sin(Cn);
            cos_Cn = cos(Cn);
            sin_Ce = sin(Ce);
            cos_Ce = cos(Ce);

            Ce = atan2(sin_Ce, cos_Ce * cos_Cn);
            Cn = atan2(sin_Cn * cos_Ce, hypot(sin_Ce, cos_Ce * cos_Cn));

            /* Gaussian LAT, LNG -> ell. LAT, LNG */
            *py = gatg(Q->cgb, PROJ_ETMERC_ORDER, Cn);
            *px = adjlon(Ce + Q->lon0);

            success += 1;
        }

        px = (double*)((char*)px + stride);
        py = (double*)((char*)py + stride);
        pz = (double*)((char*)pz + stride);
    }

    return success;
}

static int tmerc_e_forward(
    const pl_ProjectionDataOpaque* opq,
    int n,
    double* px,
    double* py,
    double* pz,
    int stride)
{
    PL_CHECK_PROJ_DATA_OPAQUE_SIZE(ParamsFwd);
    const ParamsFwd* Q = (const ParamsFwd*)opq;
    int success = 0;

    for (int i = 0; i < n; ++i)
    {
        double phi = *py;
        double lam = adjlon(*px - Q->lon0);

        double sin_Cn, cos_Cn, cos_Ce, sin_Ce, dCn, dCe;
        double Cn = phi, Ce = lam;

        /* ell. LAT, LNG -> Gaussian LAT, LNG */
        Cn = gatg(Q->cbg, PROJ_ETMERC_ORDER, Cn);

        /* Gaussian LAT, LNG -> compl. sph. LAT */
        sin_Cn = sin(Cn);
        cos_Cn = cos(Cn);
        sin_Ce = sin(Ce);
        cos_Ce = cos(Ce);

        Cn = atan2(sin_Cn, cos_Ce * cos_Cn);
        Ce = atan2(sin_Ce * cos_Cn, hypot(sin_Cn, cos_Cn * cos_Ce));

        /* compl. sph. N, E -> ell. norm. N, E */
        Ce = asinh(tan(Ce)); /* Replaces: Ce  = log(tan(FORTPI + Ce*0.5)); */
        Cn += clenS(Q->gtu, PROJ_ETMERC_ORDER, 2 * Cn, 2 * Ce, &dCn, &dCe);
        Ce += dCe;
        if (fabs(Ce) <= 2.623395162778)
        {
            *py = (Q->Qn * Cn + Q->Zb) * Q->projected_scale + Q->y0; /* Northing */
            *px = (Q->Qn * Ce) * Q->projected_scale + Q->x0;         /* Easting  */
            success += 1;
        }

        px = (double*)((char*)px + stride);
        py = (double*)((char*)py + stride);
        pz = (double*)((char*)pz + stride);
    }

    return success;
}

static pl_Result bake_params_common(
    const pl_Crs* crs,
    double cgb[PROJ_ETMERC_ORDER],
    double cbg[PROJ_ETMERC_ORDER],
    double utg[PROJ_ETMERC_ORDER],
    double gtu[PROJ_ETMERC_ORDER],
    double* Qn,
    double* Zb)
{
    double es = 1 - (crs->b * crs->b) / (crs->a * crs->a);

    if (es <= 0)
    {
        return pl_Result_UnknownEllipsoid;
    }

    /* flattening */
    double f = es / (1 + sqrt(1 - es)); /* Replaces: f = 1 - sqrt(1-P->es); */

    /* third flattening */
    double np, n;
    np = n = f / (2 - f);

    /* COEF. OF TRIG SERIES GEO <-> GAUSS */
    /* cgb := Gaussian -> Geodetic, KW p190 - 191 (61) - (62) */
    /* cbg := Geodetic -> Gaussian, KW p186 - 187 (51) - (52) */
    /* PROJ_ETMERC_ORDER = 6th degree : Engsager and Poder: ICC2007 */

    cgb[0] = n
        * (2
           + n * (-2 / 3.0 + n * (-2 + n * (116 / 45.0 + n * (26 / 45.0 + n * (-2854 / 675.0))))));
    cbg[0] = n
        * (-2
           + n
               * (2 / 3.0
                  + n * (4 / 3.0 + n * (-82 / 45.0 + n * (32 / 45.0 + n * (4642 / 4725.0))))));
    np *= n;
    cgb[1] = np
        * (7 / 3.0 + n * (-8 / 5.0 + n * (-227 / 45.0 + n * (2704 / 315.0 + n * (2323 / 945.0)))));
    cbg[1] = np
        * (5 / 3.0 + n * (-16 / 15.0 + n * (-13 / 9.0 + n * (904 / 315.0 + n * (-1522 / 945.0)))));
    np *= n;
    /* n^5 coeff corrected from 1262/105 -> -1262/105 */
    cgb[2] = np * (56 / 15.0 + n * (-136 / 35.0 + n * (-1262 / 105.0 + n * (73814 / 2835.0))));
    cbg[2] = np * (-26 / 15.0 + n * (34 / 21.0 + n * (8 / 5.0 + n * (-12686 / 2835.0))));
    np *= n;
    /* n^5 coeff corrected from 322/35 -> 332/35 */
    cgb[3] = np * (4279 / 630.0 + n * (-332 / 35.0 + n * (-399572 / 14175.0)));
    cbg[3] = np * (1237 / 630.0 + n * (-12 / 5.0 + n * (-24832 / 14175.0)));
    np *= n;
    cgb[4] = np * (4174 / 315.0 + n * (-144838 / 6237.0));
    cbg[4] = np * (-734 / 315.0 + n * (109598 / 31185.0));
    np *= n;
    cgb[5] = np * (601676 / 22275.0);
    cbg[5] = np * (444337 / 155925.0);

    /* Constants of the projections */
    /* Transverse Mercator (UTM, ITM, etc) */
    np = n * n;
    /* Norm. mer. quad, K&W p.50 (96), p.19 (38b), p.5 (2) */
    *Qn = crs->params.k_0 / (1 + n) * (1 + np * (1 / 4.0 + np * (1 / 64.0 + np / 256.0)));
    /* coef of trig series */
    /* utg := ell. N, E -> sph. N, E,  KW p194 (65) */
    /* gtu := sph. N, E -> ell. N, E,  KW p196 (69) */
    utg[0] = n
        * (-0.5
           + n
               * (2 / 3.0
                  + n
                      * (-37 / 96.0
                         + n * (1 / 360.0 + n * (81 / 512.0 + n * (-96199 / 604800.0))))));
    gtu[0] = n
        * (0.5
           + n
               * (-2 / 3.0
                  + n * (5 / 16.0 + n * (41 / 180.0 + n * (-127 / 288.0 + n * (7891 / 37800.0))))));
    utg[1] = np
        * (-1 / 48.0
           + n * (-1 / 15.0 + n * (437 / 1440.0 + n * (-46 / 105.0 + n * (1118711 / 3870720.0)))));
    gtu[1] = np
        * (13 / 48.0
           + n * (-3 / 5.0 + n * (557 / 1440.0 + n * (281 / 630.0 + n * (-1983433 / 1935360.0)))));
    np *= n;
    utg[2] = np * (-17 / 480.0 + n * (37 / 840.0 + n * (209 / 4480.0 + n * (-5569 / 90720.0))));
    gtu[2] =
        np * (61 / 240.0 + n * (-103 / 140.0 + n * (15061 / 26880.0 + n * (167603 / 181440.0))));
    np *= n;
    utg[3] = np * (-4397 / 161280.0 + n * (11 / 504.0 + n * (830251 / 7257600.0)));
    gtu[3] = np * (49561 / 161280.0 + n * (-179 / 168.0 + n * (6601661 / 7257600.0)));
    np *= n;
    utg[4] = np * (-4583 / 161280.0 + n * (108847 / 3991680.0));
    gtu[4] = np * (34729 / 80640.0 + n * (-3418889 / 1995840.0));
    np *= n;
    utg[5] = np * (-20648693 / 638668800.0);
    gtu[5] = np * (212378941 / 319334400.0);

    /* Gaussian latitude value of the origin latitude */
    double Z = gatg(cbg, PROJ_ETMERC_ORDER, crs->params.lat_0);

    /* Origin northing minus true northing at the origin latitude */
    /* i.e. true northing = N - P->Zb                         */
    *Zb = -*Qn * (Z + clens(gtu, PROJ_ETMERC_ORDER, 2 * Z));
    return pl_Result_Ok;
}

static pl_Result bake_params_inv(const pl_Crs* crs, ParamsInv* Q)
{
    Q->projected_scale = 1.0 / crs->a;
    Q->x0 = crs->params.x_0;
    Q->y0 = crs->params.y_0;
    Q->lat0 = crs->params.lat_0;
    Q->lon0 = crs->params.lon_0;

    double cbg[PROJ_ETMERC_ORDER];
    double gtu[PROJ_ETMERC_ORDER];
    return bake_params_common(crs, Q->cgb, cbg, Q->utg, gtu, &Q->Qn, &Q->Zb);
}

static pl_Result bake_params_fwd(const pl_Crs* crs, ParamsFwd* Q)
{
    Q->projected_scale = crs->a;
    Q->x0 = crs->params.x_0;
    Q->y0 = crs->params.y_0;
    Q->lat0 = crs->params.lat_0;
    Q->lon0 = crs->params.lon_0;

    double cgb[PROJ_ETMERC_ORDER];
    double utg[PROJ_ETMERC_ORDER];
    return bake_params_common(crs, cgb, Q->cbg, utg, Q->gtu, &Q->Qn, &Q->Zb);
}

PL_BAKE_FWD_PROJ_FN(utm)
{
    if (crs->a <= crs->b)
    {
        return pl_Result_EllipsoidRequired;
    }

    PL_CHECK_PROJ_DATA_OPAQUE_SIZE(ParamsFwd);
    ParamsFwd* params = (ParamsFwd*)opq;

    pl_Result res = bake_params_fwd(crs, params);
    if (res != pl_Result_Ok) return res;

    *fn = tmerc_e_forward;

    return pl_Result_Ok;
}

PL_BAKE_INV_PROJ_FN(utm)
{
    if (crs->a <= crs->b)
    {
        return pl_Result_EllipsoidRequired;
    }

    PL_CHECK_PROJ_DATA_OPAQUE_SIZE(ParamsInv);
    ParamsInv* params = (ParamsInv*)opq;

    pl_Result res = bake_params_inv(crs, params);
    if (res != pl_Result_Ok) return res;

    *fn = tmerc_e_inverse;

    return pl_Result_Ok;
}
