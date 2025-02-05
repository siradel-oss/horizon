#pragma once

#include <stdbool.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum pl_ProjectionType
    {
        pl_ProjectionType_Unknown,
        pl_ProjectionType_LatLong,
        pl_ProjectionType_Geocent,
        pl_ProjectionType_Merc,
        pl_ProjectionType_Lcc,
        pl_ProjectionType_TMerc,
        pl_ProjectionType_Utm,
        _pl_ProjectionType_Count
    } pl_ProjectionType;

    // All projection parameters must be POD.
    // This entire structure should be memcmp-able to
    // deduce that both projection instances are the same.
    // Note that since we use unions, some of the memory can be different
    // while still describing the same projection. This is why it's important
    // to clear this to 0 before filling it in.
    // Anyway a false negative won't break everything, it just will be slower.
    typedef struct pl_ProjectionParams
    {
        pl_ProjectionType type;

        char _padding[4];

        double x_0;
        double y_0;
        double lat_0;
        double lon_0;
        double k_0;

        union
        {
            struct
            {
                bool has_lat_ts;
                char _padding[7];
                double lat_ts;
                char _padding2[8];
            } merc;

            struct
            {
                double lat_1;
                double lat_2;
                bool has_lat_2;
                bool has_lat_0;
                char _padding[6];
            } lcc;

            struct
            {
                int zone;
                bool south;
                bool has_zone;
                char _padding[18];
            } utm;
        };
    } pl_ProjectionParams;

    typedef enum pl_Axis
    {
        pl_Axis_West,
        pl_Axis_East,
        pl_Axis_North,
        pl_Axis_South,
        pl_Axis_Up,
        pl_Axis_Down,
    } pl_Axis;

    typedef enum pl_DatumXformType
    {
        pl_DatumXformType_None,
        pl_DatumXformType_Offset,
        pl_DatumXformType_Helmert,
        pl_DatumXformType_GridShift,
    } pl_DatumXformType;

    typedef struct pl_DatumXform
    {
        pl_DatumXformType type;
        char _padding[4];

        union
        {
            struct
            {
                double x;
                double y;
                double z;
                char _padding[32];
            } offset;

            struct
            {
                double x;
                double y;
                double z;
                double rx;
                double ry;
                double rz;
                double s;
            } helmert;

            double values[7];
        };
    } pl_DatumXform;

    typedef struct pl_Crs
    {
        double a;                  // Semi-major axis
        double b;                  // Semi-minor axis
        double rf;                 // Inverse flattening
        pl_DatumXform datum_xform; // Datum transform to WGS84
        double to_meter;           // For projected CRS only
        double to_radian;          // For geodetic CRS only
        pl_ProjectionParams params;
        double prime_meridian; // In radians
        pl_Axis x_axis;
        pl_Axis y_axis;
        pl_Axis z_axis;
        char _padding[4];
    } pl_Crs;

    bool pl_are_crs_equal(const pl_Crs* a, const pl_Crs* b);

    typedef enum pl_TransformFlags
    {
        pl_TransformFlags_AdjustSrcAxis_Bit = 0x0001,
        pl_TransformFlags_AdjustDstAxis_Bit = 0x0002,
        pl_TransformFlags_Reproject_Bit = 0x0004,
        pl_TransformFlags_ConvertEllipsoid_Bit = 0x0008,
        pl_TransformFlags_ShiftDatum_Bit = 0x0010,

        pl_TransformFlags_AdjustSrcAxis = pl_TransformFlags_AdjustSrcAxis_Bit,
        pl_TransformFlags_AdjustDstAxis = pl_TransformFlags_AdjustDstAxis_Bit,
        pl_TransformFlags_Reproject = pl_TransformFlags_Reproject_Bit,
        pl_TransformFlags_ConvertEllipsoid =
            pl_TransformFlags_Reproject | pl_TransformFlags_ConvertEllipsoid_Bit,
        pl_TransformFlags_ShiftDatum =
            pl_TransformFlags_ConvertEllipsoid | pl_TransformFlags_ShiftDatum_Bit,
    } pl_TransformFlags;

    // Matrices are column-major
    // Affine matrices have translation as last column.

    typedef struct pl_ProjectionDataOpaque
    {
        double _[24];
    } pl_ProjectionDataOpaque;

    typedef int (*pl_TransformFn)(
        const pl_ProjectionDataOpaque* opq,
        int n,
        double* x,
        double* y,
        double* z,
        int stride);

    typedef struct pl_Transform
    {
        unsigned int flags;

        double src_axis_adjust[3][3]; // Local -> global
        double dst_axis_adjust[3][3]; // Global -> local

        // Opaque structures for each projection to bake in parameters if they need it.
        // It doesn't necessarily contains doubles, it's just to align the fields
        // to 8 bytes, so it can be reinterpreted to anything. That anything must
        // be POD though.
        pl_ProjectionDataOpaque src_inv_opaque;
        pl_ProjectionDataOpaque dst_fwd_opaque;

        pl_TransformFn src_inv_transform;
        pl_TransformFn dst_fwd_transform;

        double src_prime_meridian;
        double dst_prime_meridian;

        // [0] = a, [1] = b
        double src_ellipsoid_axis[2];
        double dst_ellipsoid_axis[2];

        // This does both forward and inverse datum shifts.
        double datum_shift[4][3];
    } pl_Transform;

    typedef enum pl_Result
    {
        pl_Result_Ok = 0,
        pl_Result_UnknownEllipsoid = 1,
        pl_Result_UnknownDatumShiftType = 2,
        pl_Result_UnknownDatum = 3,
        pl_Result_UnknownUnit = 4,
        pl_Result_UnknownPrimeMeridian = 5,
        pl_Result_UnknownProjection = 6,
        pl_Result_InvalidAxis = 7,
        pl_Result_UnknownNadgridsValue = 8,
        pl_Result_LatitudeOutOfRange = 9,
        pl_Result_LatTsLargerThan90 = 10,
        pl_Result_ProjectionError = 11,
        pl_Result_NonConvInvMeriDist = 12,
        pl_Result_EllipsoidRequired = 13,
        pl_Result_InvalidUtmZone = 14,
        pl_Result_ConicLatEqual = 15,
        pl_Result_Lat1Or2ZeroOr90 = 16,
        pl_Result_InvalidEccentricity = 17,
    } pl_Result;

    const char* pl_result_string(pl_Result result);

    // str must be null-terminated
    pl_Result pl_crs_from_proj_zstr(const char* str, pl_Crs* crs);

    pl_Result pl_crs_from_proj_str(const char* str, size_t str_length, pl_Crs* crs);

    pl_Result pl_bake_transform(const pl_Crs* src, const pl_Crs* dst, pl_Transform* transform);

    pl_Result pl_transform(
        const pl_Transform* transform,
        int n,
        const double* in_x,
        const double* in_y,
        const double* in_z,
        int in_stride, // In bytes
        double* out_x,
        double* out_y,
        double* out_z,
        int out_stride); // In bytes

    inline pl_Result pl_transform_in_place(
        const pl_Transform* transform,
        int n,
        double* x,
        double* y,
        double* z,
        int stride) // In bytes
    {
        return pl_transform(transform, n, x, y, z, stride, x, y, z, stride);
    }

    inline pl_Result pl_transform_in_place_canonical(
        const pl_Transform* transform,
        int n,
        double* stream)
    {
        return pl_transform_in_place(
            transform, n, stream, stream + 1, stream + 2, sizeof(double) * 3);
    }

    inline pl_Result pl_transform_canonical(
        const pl_Transform* transform,
        int n,
        const double* in,
        double* out)
    {
        return pl_transform(
            transform, n, in, in + 1, in + 2, sizeof(double) * 3, out, out + 1, out + 2,
            sizeof(double) * 3);
    }

    typedef struct pl_CrsDatabase pl_CrsDatabase;

    typedef enum pl_CrsDatabaseResult
    {
        pl_CrsDatabaseResult_Ok = 0,
        pl_CrsDatabaseResult_CrsNotFound = 1,
        pl_CrsDatabaseResult_UnsupportedCrs = 2,
        pl_CrsDatabaseResult_BufferTooSmall = 3,
    } pl_CrsDatabaseResult;

    pl_CrsDatabase* pl_load_crs_database();
    void pl_destroy_crs_database(pl_CrsDatabase* db);
    pl_CrsDatabaseResult pl_get_crs(
        const pl_CrsDatabase* db,
        const char* authority,
        unsigned int srid,
        pl_Crs* out_crs);
    pl_CrsDatabaseResult pl_get_crs_proj_str_length(
        const pl_CrsDatabase* db,
        const char* authority,
        unsigned int srid,
        size_t* out_length);
    pl_CrsDatabaseResult pl_get_crs_proj_str(
        const pl_CrsDatabase* db,
        const char* authority,
        unsigned int srid,
        char* out_proj_str,
        size_t* out_proj_str_length,
        size_t proj_str_buffer_size);

#ifdef __cplusplus
}
#endif
