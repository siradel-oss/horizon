#include "internal/projection.h"

#include "internal/constants.h"

// Implemented
// tmerc        1839
// utm          970
// lcc          740
// latlong      462
// merc         2

// To implement (+ count)
// stere        32
// aea          24
// cass         20
// sterea       19
// omerc        18
// laea         12
// somerc       5
// poly         4
// cea          2
// eqc          2
// krovak       2
// nzmg         1

PL_BAKE_FWD_PROJ_FN(latlong);
PL_BAKE_INV_PROJ_FN(latlong);

PL_REG_PROJ_PARAM_FN(merc);
PL_BAKE_FWD_PROJ_FN(merc);
PL_BAKE_INV_PROJ_FN(merc);

PL_BAKE_FWD_PROJ_FN(tmerc);
PL_BAKE_INV_PROJ_FN(tmerc);

PL_REG_PROJ_PARAM_FN(utm);
PL_BAKE_FWD_PROJ_FN(utm);
PL_BAKE_INV_PROJ_FN(utm);

PL_REG_PROJ_PARAM_FN(lcc);
PL_BAKE_FWD_PROJ_FN(lcc);
PL_BAKE_INV_PROJ_FN(lcc);

PL_BAKE_FWD_PROJ_FN(geocent);
PL_BAKE_INV_PROJ_FN(geocent);

static const pl_Projection PROJECTIONS[_pl_ProjectionType_Count] = {
    [pl_ProjectionType_Unknown] =
        {
            "<Unknown>",
            NULL,
            NULL,
            NULL,
        },
    [pl_ProjectionType_Geocent] =
        {"Geocentric", NULL, PL_BAKE_FWD_PROJ(geocent), PL_BAKE_INV_PROJ(geocent)},
    [pl_ProjectionType_LatLong] =
        {"Lat/long (Geodetic)", NULL, PL_BAKE_FWD_PROJ(latlong), PL_BAKE_INV_PROJ(latlong)},
    [pl_ProjectionType_Merc] =
        {"Mercator", PL_REG_PROJ_PARAM(merc), PL_BAKE_FWD_PROJ(merc), PL_BAKE_INV_PROJ(merc)},
    [pl_ProjectionType_Lcc] =
        {"Lambert Conformal Conic", PL_REG_PROJ_PARAM(lcc), PL_BAKE_FWD_PROJ(lcc),
         PL_BAKE_INV_PROJ(lcc)},
    [pl_ProjectionType_TMerc] =
        {"Transverse Mercator", NULL, PL_BAKE_FWD_PROJ(tmerc), PL_BAKE_INV_PROJ(tmerc)},
    [pl_ProjectionType_Utm] = {
        "Universal Transverse Mercator (UTM)", PL_REG_PROJ_PARAM(utm), PL_BAKE_FWD_PROJ(utm),
        PL_BAKE_INV_PROJ(utm)}};

typedef struct pl_ProjectionMap
{
    const char* proj_id;
    pl_ProjectionType type;
} pl_ProjectionMap;

const pl_ProjectionMap PROJECTION_MAPS[] = {
    {"latlong", pl_ProjectionType_LatLong}, {"latlon", pl_ProjectionType_LatLong},
    {"longlat", pl_ProjectionType_LatLong}, {"lonlat", pl_ProjectionType_LatLong},
    {"merc", pl_ProjectionType_Merc},       {"lcc", pl_ProjectionType_Lcc},
    {"tmerc", pl_ProjectionType_TMerc},     {"utm", pl_ProjectionType_Utm},
    {"geocent", pl_ProjectionType_Geocent},
};

const pl_Projection* pl_get_projection(pl_ProjectionType type)
{
    return &PROJECTIONS[type];
}

const pl_Projection* pl_find_projection(pl_StringSpan ref)
{
    return &PROJECTIONS[pl_find_projection_type(ref)];
}

pl_ProjectionType pl_find_projection_type(pl_StringSpan ref)
{
    static const int COUNT = sizeof(PROJECTION_MAPS) / sizeof(PROJECTION_MAPS[0]);
    for (int i = 0; i < COUNT; ++i)
    {
        if (pl_ss_compare(&ref, PROJECTION_MAPS[i].proj_id))
        {
            return PROJECTION_MAPS[i].type;
        }
    }
    return pl_ProjectionType_Unknown;
}
