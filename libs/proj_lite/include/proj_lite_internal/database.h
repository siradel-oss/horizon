#pragma once

#include "proj_lite_internal/string_span.h"

typedef struct pl_Ellipsoid
{
    const char* proj_id;
    double a;  // Semi-major axis
    double rf; // Inverse flattening
    const char* name;
} pl_Ellipsoid;

typedef struct pl_PrimeMeridian
{
    const char* proj_id;
    const char* name;
    double lon;
} pl_PrimeMeridian;

typedef struct pl_MeasurementUnit
{
    const char* proj_id;
    const char* name;
    double conv_factor; // To meters or radians
} pl_MeasurementUnit;

typedef struct pl_Datum
{
    const char* proj_id;
    const char* ellipsoid_id;
    int value_count;
    double values[7]; // Datum shift to WGS84
} pl_Datum;

const pl_Datum* pl_db_find_datum(pl_StringSpan ref);
const pl_Ellipsoid* pl_db_find_ellipsoid(pl_StringSpan ref);
const pl_MeasurementUnit* pl_db_find_length_unit(pl_StringSpan ref);
const pl_MeasurementUnit* pl_db_find_angle_unit(pl_StringSpan ref);
const pl_PrimeMeridian* pl_db_find_prime_meridian(pl_StringSpan ref);
