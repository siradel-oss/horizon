// SPDX-FileCopyrightText: Copyright 2019 Siradel
// SPDX-License-Identifier: MIT

#include "internal/constants.h"
#include "internal/database.h"
#include "internal/projection.h"
#include "proj_lite.h"

#include <stdio.h>

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool char_to_axis(char c, pl_Axis* axis)
{
    switch (c)
    {
        case 'E':
        case 'e': *axis = pl_Axis_East; return true;
        case 'W':
        case 'w': *axis = pl_Axis_West; return true;
        case 'N':
        case 'n': *axis = pl_Axis_North; return true;
        case 'S':
        case 's': *axis = pl_Axis_South; return true;
        case 'U':
        case 'u': *axis = pl_Axis_Up; return true;
        case 'D':
        case 'd': *axis = pl_Axis_Down; return true;
        default: return false;
    }
}

static void apply_ellipsoid(pl_Crs* crs, const pl_Ellipsoid* ellps)
{
    crs->a = ellps->a;

    if (ellps->rf != 0.0)
    {
        crs->rf = ellps->rf;
        crs->b = crs->a * (1.0 - 1.0 / crs->rf);
    }
    else
    {
        crs->b = ellps->a;
    }
}

static pl_Result apply_datum(pl_Crs* crs, const pl_Datum* datum)
{
    pl_StringSpan ellps_name = pl_ss_from_zstr(datum->ellipsoid_id);
    const pl_Ellipsoid* ellps = pl_db_find_ellipsoid(ellps_name);
    if (ellps)
    {
        apply_ellipsoid(crs, ellps);
    }
    else
    {
        return pl_Result_UnknownEllipsoid;
    }

    if (datum->value_count == 0)
    {
        crs->datum_xform.type = pl_DatumXformType_None;
    }
    else if (datum->value_count == 3)
    {
        crs->datum_xform.type = pl_DatumXformType_Offset;
        crs->datum_xform.offset.x = datum->values[0];
        crs->datum_xform.offset.y = datum->values[1];
        crs->datum_xform.offset.z = datum->values[2];
    }
    else if (datum->value_count == 7)
    {
        crs->datum_xform.type = pl_DatumXformType_Helmert;
        crs->datum_xform.helmert.x = datum->values[0];
        crs->datum_xform.helmert.y = datum->values[1];
        crs->datum_xform.helmert.z = datum->values[2];
        crs->datum_xform.helmert.rx = datum->values[3];
        crs->datum_xform.helmert.ry = datum->values[4];
        crs->datum_xform.helmert.rz = datum->values[5];
        crs->datum_xform.helmert.s = datum->values[6];
    }
    else
    {
        return pl_Result_UnknownDatumShiftType;
    }

    return pl_Result_Ok;
}

static bool maybe_apply_projection_param(pl_Crs* crs, pl_StringSpan param, pl_StringSpan value)
{
    bool applied = false;
    if (pl_ss_compare(&param, "lat_0"))
    {
        crs->params.lat_0 = pl_ss_to_double(&value) * DEG2RAD;
        applied = true;
    }
    else if (pl_ss_compare(&param, "x_0"))
    {
        crs->params.x_0 = pl_ss_to_double(&value);
        applied = true;
    }
    else if (pl_ss_compare(&param, "y_0"))
    {
        crs->params.y_0 = pl_ss_to_double(&value);
        applied = true;
    }
    else if (pl_ss_compare(&param, "lon_0"))
    {
        crs->params.lon_0 = pl_ss_to_double(&value) * DEG2RAD;
        applied = true;
    }
    else if (pl_ss_compare(&param, "k_0") || pl_ss_compare(&param, "k"))
    {
        crs->params.k_0 = pl_ss_to_double(&value);
        applied = true;
    }

    const pl_Projection* proj = pl_get_projection(crs->params.type);
    if (proj->param_fn)
    {
        applied = proj->param_fn(&crs->params, param, value) || applied;
    }

    return applied;
}

static void fixup_datum_xform(pl_Crs* crs)
{
    if (crs->datum_xform.type == pl_DatumXformType_Helmert && crs->datum_xform.helmert.rx == 0.0
        && crs->datum_xform.helmert.ry == 0.0 && crs->datum_xform.helmert.rz == 0.0
        && crs->datum_xform.helmert.s == 0.0)
    {
        crs->datum_xform.type = pl_DatumXformType_Offset;
    }
}

static void fixup_ellipsoid(pl_Crs* crs)
{
    if (crs->rf == 0.0 && crs->a > 0.0 && crs->b > 0.0 && crs->a != crs->b)
    {
        crs->rf = crs->a / (crs->a - crs->b);
    }
}

pl_Result pl_crs_from_proj_zstr(const char* str, pl_Crs* crs)
{
    return pl_crs_from_proj_str(str, strlen(str), crs);
}

pl_Result pl_crs_from_proj_str(const char* crs_str, size_t str_length, pl_Crs* crs)
{
    pl_StringSpan remaining_span = pl_ss_from_str_length(crs_str, str_length);
    pl_ss_split_at_first(&remaining_span, '+');

    memset(crs, 0, sizeof(pl_Crs));
    crs->to_meter = 1.0;
    crs->to_radian = DEG2RAD; // We assume all geodetic CRS are in degrees by default.
    crs->params.k_0 = 1.0;
    crs->x_axis = pl_Axis_East;
    crs->y_axis = pl_Axis_North;
    crs->z_axis = pl_Axis_Up;

    bool has_ellps = false;
    bool has_datum = false;

    while (!pl_ss_empty(&remaining_span))
    {
        remaining_span.begin += 1;

        pl_StringSpan value = pl_ss_split_at_first(&remaining_span, '+');
        pl_StringSpan param = pl_ss_split_at_first(&value, '=');

        pl_ss_trim(&value);
        pl_ss_trim(&param);

        if (!pl_ss_empty(&value))
        {
            value.begin += 1;
        }

        bool applied_projection_param = maybe_apply_projection_param(crs, param, value);

        if (pl_ss_compare(&param, "a"))
        {
            crs->a = pl_ss_to_double(&value);
            has_ellps = true;
        }
        else if (pl_ss_compare(&param, "b"))
        {
            crs->b = pl_ss_to_double(&value);
            has_ellps = true;
        }
        else if (pl_ss_compare(&param, "ellps"))
        {
            const pl_Ellipsoid* ellps = pl_db_find_ellipsoid(value);
            if (ellps)
            {
                apply_ellipsoid(crs, ellps);
                has_ellps = true;
            }
            else
            {
                return pl_Result_UnknownEllipsoid;
            }
        }
        else if (pl_ss_compare(&param, "datum"))
        {
            const pl_Datum* datum = pl_db_find_datum(value);
            if (datum)
            {
                apply_datum(crs, datum);
                has_datum = true;
            }
            else
            {
                return pl_Result_UnknownDatum;
            }
        }
        else if (pl_ss_compare(&param, "towgs84"))
        {
            pl_StringSpan value_iter = value;

            int arg_count = 0;
            while (arg_count < 7)
            {
                pl_StringSpan p = pl_ss_split_at_first(&value_iter, ',');
                crs->datum_xform.values[arg_count++] = pl_ss_to_double(&p);

                if (pl_ss_empty(&value_iter))
                {
                    break;
                }

                value_iter.begin += 1;
            }

            if (arg_count == 7)
            {
                crs->datum_xform.type = pl_DatumXformType_Helmert;
                has_datum = true;
            }
            else
            {
                crs->datum_xform.type = pl_DatumXformType_Offset;
                has_datum = true;
            }
        }
        else if (pl_ss_compare(&param, "units"))
        {
            const pl_MeasurementUnit* unit = pl_db_find_length_unit(value);
            if (unit)
            {
                crs->to_meter = unit->conv_factor;
            }
            else
            {
                unit = pl_db_find_angle_unit(value);
                if (unit)
                {
                    crs->to_radian = unit->conv_factor;
                }
                else
                {
                    return pl_Result_UnknownUnit;
                }
            }
        }
        else if (pl_ss_compare(&param, "to_meter"))
        {
            crs->to_meter = pl_ss_to_double(&value);
        }
        else if (pl_ss_compare(&param, "pm"))
        {
            const pl_PrimeMeridian* pm = pl_db_find_prime_meridian(value);
            if (pm)
            {
                crs->prime_meridian = pm->lon;
            }
            else if (*value.begin == '-' || is_digit(*value.begin))
            {
                crs->prime_meridian = pl_ss_to_double(&value) * DEG2RAD;
            }
            else
            {
                return pl_Result_UnknownPrimeMeridian;
            }
        }
        else if (pl_ss_compare(&param, "proj"))
        {
            pl_ProjectionType type = pl_find_projection_type(value);
            if (type != pl_ProjectionType_Unknown)
            {
                crs->params.type = type;
            }
            else
            {
                return pl_Result_UnknownProjection;
            }
        }
        else if (pl_ss_compare(&param, "axis"))
        {
            if (pl_ss_size(&value) >= 3)
            {
                int num_we = 0;
                int num_ns = 0;
                int num_ud = 0;

                for (int i = 0; i < 3; ++i)
                {
                    switch (value.begin[i])
                    {
                        case 'e':
                        case 'w': num_we += 1; break;
                        case 'n':
                        case 's': num_ns += 1; break;
                        case 'u':
                        case 'd': num_ud += 1; break;
                        default: return pl_Result_InvalidAxis;
                    }
                }

                // We check that the basis is orthonormal so that
                // both the forward and inverse transform are defined.
                // Also this means the inverse matrix in the transposed forward.
                if (num_we != 1 || num_ns != 1 || num_ud != 1)
                {
                    return pl_Result_InvalidAxis;
                }

                bool success = char_to_axis(value.begin[0], &crs->x_axis)
                    && char_to_axis(value.begin[1], &crs->y_axis)
                    && char_to_axis(value.begin[2], &crs->z_axis);

                if (!success)
                {
                    return pl_Result_InvalidAxis;
                }
            }
            else
            {
                return pl_Result_InvalidAxis;
            }
        }
        else if (pl_ss_compare(&param, "nadgrids"))
        {
            crs->datum_xform.type = pl_DatumXformType_GridShift;

            if (!pl_ss_compare(&value, "@null"))
            {
                return pl_Result_UnknownNadgridsValue;
            }
        }
        else if (pl_ss_compare(&param, "no_defs") || pl_ss_compare(&param, "wktext"))
        {
            // noop
        }
        else if (!applied_projection_param)
        {
            fprintf(
                stderr, "Unhandled parameter: %.*s = %.*s\n", (int)pl_ss_size(&param), param.begin,
                (int)pl_ss_size(&value), value.begin);
        }
    }

    if (!has_ellps && !has_datum)
    {
        const pl_StringSpan ss = pl_ss_from_zstr("WGS84");
        const pl_Datum* datum = pl_db_find_datum(ss);
        if (datum)
        {
            apply_datum(crs, datum);
        }
    }

    fixup_datum_xform(crs);
    fixup_ellipsoid(crs);

    return pl_Result_Ok;
}
