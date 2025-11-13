#include "internal/common.h"
#include "internal/constants.h"
#include "internal/projection.h"
#include "proj_lite.h"

#include <math.h>

static double AXIS_SIGN[] = {[pl_Axis_West] = -1,  [pl_Axis_East] = 1, [pl_Axis_North] = 1,
                             [pl_Axis_South] = -1, [pl_Axis_Up] = 1,   [pl_Axis_Down] = -1};

static int AXIS_INDEX[] = {[pl_Axis_West] = 0,  [pl_Axis_East] = 0, [pl_Axis_North] = 1,
                           [pl_Axis_South] = 1, [pl_Axis_Up] = 2,   [pl_Axis_Down] = 2};

static void bake_local_to_global_axis_adjust(
    const pl_Crs* crs,
    double adjust[3][3],
    double unit_xy,
    double unit_z)
{
    pl_Axis axis[3] = {crs->x_axis, crs->y_axis, crs->z_axis};

    for (int i = 0; i < 3; ++i)
    {
        double unit = unit_xy;
        if (i == 2) unit = unit_z;

        adjust[i][AXIS_INDEX[axis[i]]] = AXIS_SIGN[axis[i]] * unit;
    }
}

static void bake_global_to_local_axis_adjust(
    const pl_Crs* crs,
    double adjust[3][3],
    double unit_xy,
    double unit_z)
{
    bake_local_to_global_axis_adjust(crs, adjust, 1.0 / unit_xy, 1.0 / unit_z);

    // Transpose matrix = invert because it is orthogonal.

#define SWAP(a, b) \
    tmp = a;       \
    a = b;         \
    b = tmp

    double tmp;
    SWAP(adjust[0][1], adjust[1][0]);
    SWAP(adjust[0][2], adjust[2][0]);
    SWAP(adjust[2][1], adjust[1][2]);

#undef SWAP
}

static bool is_axis_adjust_identity(const pl_Crs* crs)
{
    return crs->x_axis == pl_Axis_East && crs->y_axis == pl_Axis_North && crs->z_axis == pl_Axis_Up;
}

static double get_crs_unit(const pl_Crs* crs)
{
    if (crs->params.type == pl_ProjectionType_LatLong)
    {
        return crs->to_radian;
    }
    else
    {
        return crs->to_meter;
    }
}

// Local -> WGS 84
static void compute_forward_datum_shift_matrix(const pl_DatumXform* xform, double matrix[4][3])
{
    memset(matrix, 0, sizeof(double) * 4 * 3);
    matrix[0][0] = 1;
    matrix[1][1] = 1;
    matrix[2][2] = 1;

    if (xform->type == pl_DatumXformType_Helmert)
    {
        double scale = (1 + 1e-6 * xform->helmert.s);

        matrix[0][0] = scale;
        matrix[0][1] = xform->helmert.rz * scale * ARCSEC_TO_RAD;
        matrix[0][2] = -xform->helmert.ry * scale * ARCSEC_TO_RAD;

        matrix[1][0] = -xform->helmert.rz * scale * ARCSEC_TO_RAD;
        matrix[1][1] = scale;
        matrix[1][2] = xform->helmert.rx * scale * ARCSEC_TO_RAD;

        matrix[2][0] = xform->helmert.ry * scale * ARCSEC_TO_RAD;
        matrix[2][1] = -xform->helmert.rx * scale * ARCSEC_TO_RAD;
        matrix[2][2] = scale;

        matrix[3][0] = xform->helmert.x;
        matrix[3][1] = xform->helmert.y;
        matrix[3][2] = xform->helmert.z;
    }
    else if (xform->type == pl_DatumXformType_Offset)
    {
        matrix[3][0] = xform->offset.x;
        matrix[3][1] = xform->offset.y;
        matrix[3][2] = xform->offset.z;
    }
}

// WGS 84 -> Local
static void compute_inverse_datum_shift_matrix(const pl_DatumXform* xform, double matrix[4][3])
{
    memset(matrix, 0, sizeof(double) * 4 * 3);
    matrix[0][0] = 1;
    matrix[1][1] = 1;
    matrix[2][2] = 1;

    if (xform->type == pl_DatumXformType_Helmert)
    {
        double scale = 1.0 / (1 + 1e-6 * xform->helmert.s);

        matrix[0][0] = scale;
        matrix[0][1] = -xform->helmert.rz * scale * ARCSEC_TO_RAD;
        matrix[0][2] = xform->helmert.ry * scale * ARCSEC_TO_RAD;

        matrix[1][0] = xform->helmert.rz * scale * ARCSEC_TO_RAD;
        matrix[1][1] = scale;
        matrix[1][2] = -xform->helmert.rx * scale * ARCSEC_TO_RAD;

        matrix[2][0] = -xform->helmert.ry * scale * ARCSEC_TO_RAD;
        matrix[2][1] = xform->helmert.rx * scale * ARCSEC_TO_RAD;
        matrix[2][2] = scale;

        double dx = xform->helmert.x;
        double dy = xform->helmert.y;
        double dz = xform->helmert.z;
        double rx = xform->helmert.rx * ARCSEC_TO_RAD;
        double ry = xform->helmert.ry * ARCSEC_TO_RAD;
        double rz = xform->helmert.rz * ARCSEC_TO_RAD;

        matrix[3][0] = (-dx - dy * rz + dz * ry) * scale;
        matrix[3][1] = (rz * dx - dy - dz * rx) * scale;
        matrix[3][2] = (-dx * ry + dy * rx - dz) * scale;
    }
    else if (xform->type == pl_DatumXformType_Offset)
    {
        matrix[3][0] = -xform->offset.x;
        matrix[3][1] = -xform->offset.y;
        matrix[3][2] = -xform->offset.z;
    }
}

static void compute_datum_shift_matrix(
    const pl_DatumXform* src,
    const pl_DatumXform* dst,
    double matrix[4][3])
{
    double f[4][3];
    compute_forward_datum_shift_matrix(src, f);

    double i[4][3];
    compute_inverse_datum_shift_matrix(dst, i);

    // Simple matrix multiplication. I just unrolled it to help the
    // compiler vectorize it, hopefully... (And also because it's sparse-ish)
    // Last line of each is (0, 0, 0, 1).

    matrix[0][0] = i[0][0] * f[0][0] + i[1][0] * f[0][1] + i[2][0] * f[0][2];
    matrix[0][1] = i[0][1] * f[0][0] + i[1][1] * f[0][1] + i[2][1] * f[0][2];
    matrix[0][2] = i[0][2] * f[0][0] + i[1][2] * f[0][1] + i[2][2] * f[0][2];

    matrix[1][0] = i[0][0] * f[1][0] + i[1][0] * f[1][1] + i[2][0] * f[1][2];
    matrix[1][1] = i[0][1] * f[1][0] + i[1][1] * f[1][1] + i[2][1] * f[1][2];
    matrix[1][2] = i[0][2] * f[1][0] + i[1][2] * f[1][1] + i[2][2] * f[1][2];

    matrix[2][0] = i[0][0] * f[2][0] + i[1][0] * f[2][1] + i[2][0] * f[2][2];
    matrix[2][1] = i[0][1] * f[2][0] + i[1][1] * f[2][1] + i[2][1] * f[2][2];
    matrix[2][2] = i[0][2] * f[2][0] + i[1][2] * f[2][1] + i[2][2] * f[2][2];

    matrix[3][0] = i[0][0] * f[3][0] + i[1][0] * f[3][1] + i[2][0] * f[3][2] + i[3][0];
    matrix[3][1] = i[0][1] * f[3][0] + i[1][1] * f[3][1] + i[2][1] * f[3][2] + i[3][1];
    matrix[3][2] = i[0][2] * f[3][0] + i[1][2] * f[3][1] + i[2][2] * f[3][2] + i[3][2];
}

static bool is_datum_xform_identity(const pl_DatumXform* xform)
{
    switch (xform->type)
    {
        case pl_DatumXformType_None:
        case pl_DatumXformType_GridShift: return true;
        case pl_DatumXformType_Offset:
            return xform->offset.x == 0 && xform->offset.y == 0 && xform->offset.z == 0;
        case pl_DatumXformType_Helmert:
            return xform->helmert.x == 0 && xform->helmert.y == 0 && xform->helmert.z == 0
                && xform->helmert.rx == 0 && xform->helmert.ry == 0 && xform->helmert.rz == 0
                && xform->helmert.s == 0;
        default: assert(!"Unhandled datum transform type"); return true;
    }
}

static bool are_projections_equal(const pl_ProjectionParams* a, const pl_ProjectionParams* b)
{
    // @Safety We are careful to have all places that initialize a pl_Crs zero it first, and those
    // places are the ones that initialize pl_ProjectionParams also.
    return memcmp(a, b, sizeof(pl_ProjectionParams)) == 0;
}

bool pl_are_crs_equal(const pl_Crs* a, const pl_Crs* b)
{
    // @Safety We are careful to have all places that initialize a pl_Crs zero it first.
    // This should only be pl_crs_from_proj_str.
    return memcmp(a, b, sizeof(pl_Crs)) == 0;
}

// Reprojection pipeline:
//            Flags
//                                                 _
//                                  AdjustSrcAxis |  Adjust source axis to ENU
//            ____________________________________|_ Convert source units to degrees or radians
//           |                                       Inverse the source projection
//           |                    __________________ Shift source prime meridian to greenwich
//           |                   |              ____ Geodetic to geocentric with source ellipsoid
// Reproject | ConvertEllipsoid  |  ShiftDatum |     Datum shift source datum to WGS84
//           |                   |             |____ Datum shift WGS84 to target datum
//           |                   |__________________ Geocentric to geodetic with target ellipsoid
//           |                                       Shift target prime meridian from greenwich
//           |______________________________________ Forward target projection
//                                                |  Convert to target units from degrees or radians
//                                  AdjustDstAxis |_ Adjust target axis from ENU

pl_Result pl_bake_transform(const pl_Crs* src, const pl_Crs* dst, pl_Transform* transform)
{
    memset(transform, 0, sizeof(pl_Transform));

    double src_unit = get_crs_unit(src);
    double dst_unit = get_crs_unit(dst);

    bake_local_to_global_axis_adjust(src, transform->src_axis_adjust, src_unit, src->to_meter);

    if (!is_axis_adjust_identity(src) || src_unit != 1.0)
    {
        transform->flags |= pl_TransformFlags_AdjustSrcAxis;
    }

    bake_global_to_local_axis_adjust(dst, transform->dst_axis_adjust, dst_unit, dst->to_meter);

    if (!is_axis_adjust_identity(dst) || dst_unit != 1.0)
    {
        transform->flags |= pl_TransformFlags_AdjustDstAxis;
    }

    if (!are_projections_equal(&src->params, &dst->params))
    {
        transform->flags |= pl_TransformFlags_Reproject;
    }

    // We bake the parameters even if we don't need to reproject
    // because we may actually need to reprojection for datum shift.
    // Anyway this shouldn't be a very heavy operation.
    const pl_Projection* src_proj = pl_get_projection(src->params.type);
    const pl_Projection* dst_proj = pl_get_projection(dst->params.type);

    if (!src_proj || !dst_proj)
    {
        return pl_Result_UnknownProjection;
    }

    if (src_proj->bake_inv)
    {
        pl_Result res =
            src_proj->bake_inv(src, &transform->src_inv_opaque, &transform->src_inv_transform);

        if (res != pl_Result_Ok) return res;
    }
    else
    {
        transform->src_inv_transform = NULL;
    }

    if (dst_proj->bake_fwd)
    {
        pl_Result res =
            dst_proj->bake_fwd(dst, &transform->dst_fwd_opaque, &transform->dst_fwd_transform);

        if (res != pl_Result_Ok) return res;
    }
    else
    {
        transform->dst_fwd_transform = NULL;
    }

    transform->src_prime_meridian = src->prime_meridian;
    transform->dst_prime_meridian = dst->prime_meridian;

    if (transform->src_prime_meridian != 0.0 || transform->dst_prime_meridian != 0.0)
    {
        transform->flags |= pl_TransformFlags_Reproject;
    }

    // We don't actually support grid shift transformation,
    // but to match proj 4 behavior, we set the ellipsoid to WGS84.

    if (src->datum_xform.type == pl_DatumXformType_GridShift)
    {
        // Set to WGS84
        transform->src_ellipsoid_axis[0] = WGS84_A;
        transform->src_ellipsoid_axis[1] = WGS84_B;
    }
    else
    {
        transform->src_ellipsoid_axis[0] = src->a;
        transform->src_ellipsoid_axis[1] = src->b;
    }

    if (dst->datum_xform.type == pl_DatumXformType_GridShift)
    {
        // Set to WGS84
        transform->dst_ellipsoid_axis[0] = WGS84_A;
        transform->dst_ellipsoid_axis[1] = WGS84_B;
    }
    else
    {
        transform->dst_ellipsoid_axis[0] = dst->a;
        transform->dst_ellipsoid_axis[1] = dst->b;
    }

    // To match proj's behavior, we don't do any ellipsoid or
    // datum transform if any of the source or target CRS
    // has no datum defined.

    bool one_has_no_datum =
        (src->datum_xform.type == pl_DatumXformType_None
         || dst->datum_xform.type == pl_DatumXformType_None);

    if ((src->a != dst->a || src->b != dst->b) && !one_has_no_datum)
    {
        transform->flags |= pl_TransformFlags_ConvertEllipsoid;
    }

    if ((!is_datum_xform_identity(&src->datum_xform) || !is_datum_xform_identity(&dst->datum_xform))
        && !one_has_no_datum)
    {
        transform->flags |= pl_TransformFlags_ShiftDatum;
        compute_datum_shift_matrix(&src->datum_xform, &dst->datum_xform, transform->datum_shift);
    }

    return pl_Result_Ok;
}

// @Todo This could be optimized when both datum shifts are offsets
static void shift_datum(const double m[4][3], int n, double* px, double* py, double* pz, int stride)
{
    for (int i = 0; i < n; ++i)
    {
        double x = *px;
        double y = *py;
        double z = *pz;

        *px = m[0][0] * x + m[1][0] * y + m[2][0] * z + m[3][0];
        *py = m[0][1] * x + m[1][1] * y + m[2][1] * z + m[3][1];
        *pz = m[0][2] * x + m[1][2] * y + m[2][2] * z + m[3][2];

        px = (double*)((char*)px + stride);
        py = (double*)((char*)py + stride);
        pz = (double*)((char*)pz + stride);
    }
}

static void convert_ellipsoid(
    const pl_Transform* transform,
    int n,
    double* px,
    double* py,
    double* pz,
    int stride)
{
    pl_geodetic_to_geocentric(
        transform->src_ellipsoid_axis[0], transform->src_ellipsoid_axis[1], n, px, py, pz, stride);

    if (transform->flags & pl_TransformFlags_ShiftDatum_Bit)
    {
        shift_datum(transform->datum_shift, n, px, py, pz, stride);
    }

    pl_geocentric_to_geodetic(
        transform->dst_ellipsoid_axis[0], transform->dst_ellipsoid_axis[1], n, px, py, pz, stride);
}

static void shift_prime_meridian(int n, double* px, int stride, double offset)
{
    for (int i = 0; i < n; ++i)
    {
        *px += offset;
        px = (double*)((char*)px + stride);
    }
}

static pl_Result reproject(
    const pl_Transform* transform,
    int n,
    double* px,
    double* py,
    double* pz,
    int stride)
{
    if (transform->src_inv_transform)
    {
        int success =
            transform->src_inv_transform(&transform->src_inv_opaque, n, px, py, pz, stride);

        if (success != n) return pl_Result_ProjectionError;
    }

    if (transform->src_prime_meridian != 0)
    {
        shift_prime_meridian(n, px, stride, transform->src_prime_meridian);
    }

    if (transform->flags & pl_TransformFlags_ConvertEllipsoid_Bit)
    {
        convert_ellipsoid(transform, n, px, py, pz, stride);
    }

    if (transform->dst_prime_meridian != 0)
    {
        shift_prime_meridian(n, px, stride, -transform->dst_prime_meridian);
    }

    if (transform->dst_fwd_transform)
    {
        int success =
            transform->dst_fwd_transform(&transform->dst_fwd_opaque, n, px, py, pz, stride);

        if (success != n) return pl_Result_ProjectionError;
    }

    return pl_Result_Ok;
}

static void adjust_axis(
    int n,
    double* px,
    double* py,
    double* pz,
    int stride,
    const double matrix[3][3])
{
    for (int i = 0; i < n; ++i)
    {
        double x = *px;
        double y = *py;
        double z = *pz;

        *px = matrix[0][0] * x + matrix[1][0] * y + matrix[2][0] * z;
        *py = matrix[0][1] * x + matrix[1][1] * y + matrix[2][1] * z;
        *pz = matrix[0][2] * x + matrix[1][2] * y + matrix[2][2] * z;

        px = (double*)((char*)px + stride);
        py = (double*)((char*)py + stride);
        pz = (double*)((char*)pz + stride);
    }
}

static bool _is_canonical(int stride, const double* x, const double* y, const double* z)
{
    return stride == 24 && y == x + 1 && z == x + 2;
}

pl_Result pl_transform(
    const pl_Transform* transform,
    int n,
    const double* in_x,
    const double* in_y,
    const double* in_z,
    int in_stride,
    double* out_x,
    double* out_y,
    double* out_z,
    int out_stride)
{
    // Initial copy, we then transform in-place.

    bool is_canonical = _is_canonical(in_stride, in_x, in_y, in_z)
        && _is_canonical(out_stride, out_x, out_y, out_z);

    if (is_canonical)
    {
        if (in_x == out_x && in_y == out_y && in_z == out_z)
        {
            // Canonical and in-place, nothing to do!
        }
        else
        {
            // Canonical but not in place, we can memcpy.
            size_t data_size = (size_t)n * 24;
            memcpy(out_x, in_x, data_size);
        }
    }
    else
    {
        // Worst case, copy everything
        const double* ix = in_x;
        const double* iy = in_y;
        const double* iz = in_z;
        double* ox = out_x;
        double* oy = out_y;
        double* oz = out_z;

        for (int i = 0; i < n; ++i)
        {
            *ox = *ix;
            *oy = *iy;
            *oz = *iz;

            ix = (const double*)((const char*)ix + in_stride);
            iy = (const double*)((const char*)iy + in_stride);
            iz = (const double*)((const char*)iz + in_stride);
            ox = (double*)((char*)ox + out_stride);
            oy = (double*)((char*)oy + out_stride);
            oz = (double*)((char*)oz + out_stride);
        }
    }

    if (transform->flags & pl_TransformFlags_AdjustSrcAxis_Bit)
    {
        adjust_axis(n, out_x, out_y, out_z, out_stride, transform->src_axis_adjust);
    }

    if (transform->flags & pl_TransformFlags_Reproject_Bit)
    {
        pl_Result res = reproject(transform, n, out_x, out_y, out_z, out_stride);
        if (res != pl_Result_Ok) return res;
    }

    if (transform->flags & pl_TransformFlags_AdjustDstAxis_Bit)
    {
        adjust_axis(n, out_x, out_y, out_z, out_stride, transform->dst_axis_adjust);
    }

    return pl_Result_Ok;
}
