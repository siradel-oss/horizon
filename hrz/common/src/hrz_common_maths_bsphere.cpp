#include "hrz_common_maths.h"

#include <hrz_fnd_random.h>
#include <hrz_fnd_static_vector.h>

#include <algorithm>

namespace
{
// See: Larsson, Thomas. "Fast and tight fitting bounding spheres." SIGRAD 2008.
// The Annual SIGRAD Conference Special Theme: Interaction; November 27-28;
// 2008 Stockholm; Sweden. No. 034. Linköping University Electronic Press, 2008.
//
// This is an implementation of EPOS-6.
//
// Basically we find the extremum points along each axis (3 axes, 6 points, hence the name).
// Then we compute an exact bounding sphere for those.
// And finally we grow it as to encompass all other points.
// The 6 version seems to be as fast as more naive algorithms while generating
// tighter fitting bounding spheres.
// The other versions add axes so that the initial bounding sphere is more representative
// of the points cloud.
//
// For the exact, slow, algorithm, see: Welzl, Emo. "Smallest enclosing disks
// (balls and ellipsoids)." New results and new trends in computer science.
// Springer, Berlin, Heidelberg, 1991. 359-370.
//
// It's a recursive algorithm but it's OK because we execute it with a very low
// number of points (6).

// https://gamedev.stackexchange.com/questions/162731/welzl-algorithm-to-find-the-smallest-bounding-sphere
template<typename T>
void _triangle_circumsphere(
    lm::Vector<T, 3> a,
    lm::Vector<T, 3> b,
    lm::Vector<T, 3> c,
    lm::Vector<T, 3>* out_center,
    T* out_radius)
{
    lm::Vector<T, 3> da = a - c;
    lm::Vector<T, 3> db = b - c;
    lm::Vector<T, 3> cross = lm::cross(da, db);

    *out_center =
        lm::cross(lm::length2(da) * db - lm::length2(db) * da, cross) / (2 * lm::length2(cross));
    *out_radius = lm::length(*out_center);
    *out_center += c;
}

// https://mathworld.wolfram.com/Circumsphere.html
template<typename T>
void _tetrahedron_circumsphere(
    lm::Vector<T, 3> a,
    lm::Vector<T, 3> b,
    lm::Vector<T, 3> c,
    lm::Vector<T, 3> d,
    lm::Vector<T, 3>* out_center,
    T* out_radius)
{
    // With very large numbers, this blows up really fast.
    // To avoid that we offset everything by the center of the
    // tetrahedron, and hope for the best...
    //      -slerouzic, 2020-11-04

    lm::Vector<T, 3> offset((a + b + c + d) * 0.25);
    a -= offset;
    b -= offset;
    c -= offset;
    d -= offset;

    lm::Matrix<T, 4> mat(
        lm::Vector<T, 4>(a, 1), lm::Vector<T, 4>(b, 1), lm::Vector<T, 4>(c, 1),
        lm::Vector<T, 4>(d, 1));

    T det_a = lm::determinant(mat);

    if (det_a == 0)
    {
        // When the tetrahedron is degenerate all the points are coplanar and the determinant is
        // zero so we can't proceed with the exact circumsphere calculation. Though, we still need
        // values for the circumsphere, so instead we emit an infinity radius so that the later
        // naive bounding sphere calculation result will be used.
        *out_radius = std::numeric_limits<T>::infinity();
        return;
    }

    mat = lm::transpose(mat);

    lm::Vector<T, 4> x_col = mat.x;
    lm::Vector<T, 4> y_col = mat.y;
    lm::Vector<T, 4> z_col = mat.z;

    mat.x = x_col * x_col + y_col * y_col + z_col * z_col;
    T det_x = lm::determinant(mat);

    mat.y = x_col;
    T det_y = -lm::determinant(mat);

    mat.z = y_col;
    T det_z = lm::determinant(mat);

    *out_center = lm::Vector<T, 3>(det_x, det_y, det_z) / (2 * det_a);
    *out_radius = lm::length(*out_center - a);
    *out_center += offset;
}

template<typename T>
void _naive_bounding_sphere(
    gsl::span<const lm::Vector<T, 3>> pts,
    lm::Vector<T, 3>* out_center,
    T* out_radius)
{
    if (pts.size() == 0)
    {
        *out_radius = -1;
        return;
    }

    lm::Bbox<T, 3> bbox(pts[0], pts[0]);
    for (const lm::Vector<T, 3>& pt : pts)
    {
        bbox = lm::expand(bbox, pt);
    }

    lm::Vector<T, 3> center = lm::center(bbox);

    T radius = lm::length(center - pts[0]);
    for (size_t i = 1; i < pts.size(); ++i)
    {
        T r = lm::length(center - pts[i]);
        radius = std::max(r, radius);
    }

    *out_radius = radius;
    *out_center = center;
}

template<typename T>
void _compute_exact_bounding_sphere_inner(
    hrz::StaticVector<lm::Vector<T, 3>, 6>& contained,
    hrz::StaticVector<lm::Vector<T, 3>, 6>& boundary,
    lm::Vector<T, 3>* out_center,
    T* out_radius)
{
    if (contained.size() == 0 || boundary.size() == 4)
    {
        switch (boundary.size())
        {
            case 0:
                *out_center = lm::Vector<T, 3>(0, 0, 0);
                *out_radius = -1;
                break;
            case 1:
                *out_center = boundary[0];
                *out_radius = 0;
                break;
            case 2:
                *out_center = (T)0.5 * (boundary[0] + boundary[1]);
                *out_radius = (T)0.5 * lm::length(boundary[0] - boundary[1]);
                break;
            case 3:
                _triangle_circumsphere(
                    boundary[0], boundary[1], boundary[2], out_center, out_radius);
                break;
            case 4:
                _tetrahedron_circumsphere(
                    boundary[0], boundary[1], boundary[2], boundary[3], out_center, out_radius);
                break;
        }

        // Sometimes, the circumsphere is not the smallest enclosing sphere, so try
        // the same thing with a naive algorithm, and see which one is better.
        // Also sometimes, especially when the distances between points comprise
        // both large and very small distances, the methods above can produce NaNs.
        // The naive method can fix them.
        if (boundary.size() >= 3)
        {
            T naive_radius;
            lm::Vector<T, 3> naive_center;
            _naive_bounding_sphere(
                gsl::span<const lm::Vector<T, 3>>(boundary), &naive_center, &naive_radius);

            if (naive_radius < *out_radius || std::isnan(out_center->x) || std::isnan(out_center->y)
                || std::isnan(out_center->z) || std::isnan(*out_radius))
            {
                *out_radius = naive_radius;
                *out_center = naive_center;
            }
        }

        return;
    }

    size_t last = contained.size() - 1;
    size_t candidate = hrz::random_int<size_t>(0, last);

    lm::Vector<T, 3> removed = contained[candidate];
    contained[candidate] = contained[last];
    contained.pop_back();

    _compute_exact_bounding_sphere_inner(contained, boundary, out_center, out_radius);

    // Removed point is not in the ball, we add it to the boundary
    if (lm::length2(*out_center - removed) > *out_radius * *out_radius)
    {
        boundary.push_back(removed);
        _compute_exact_bounding_sphere_inner(contained, boundary, out_center, out_radius);
        boundary.pop_back();
    }

    contained.push_back(removed);
}

template<typename T>
void _compute_exact_bounding_sphere(
    gsl::span<const lm::Vector<T, 3>> in_pts,
    lm::Vector<T, 3>* out_center,
    T* out_radius)
{
    hrz::StaticVector<lm::Vector<T, 3>, 6> contained;
    hrz::StaticVector<lm::Vector<T, 3>, 6> boundary;

    for (const auto& p : in_pts)
    {
        contained.push_back(p);
    }

    // Deduplicate the extremum points
    std::sort(
        contained.begin(), contained.end(),
        [](const lm::Vector<T, 3>& a, const lm::Vector<T, 3>& b) -> bool
        {
            if (a.x != b.x) return a.x < b.x;
            if (a.y != b.y) return a.y < b.y;
            return a.z < b.z;
        });

    auto unique_it = std::unique(
        contained.begin(), contained.end(),
        [](const lm::Vector<T, 3>& a, const lm::Vector<T, 3>& b) -> bool { return a == b; });
    contained.set_size(std::distance(contained.begin(), unique_it));

    if (contained.size() <= 4)
    {
        _compute_exact_bounding_sphere_inner(boundary, contained, out_center, out_radius);
    }
    else
    {
        _compute_exact_bounding_sphere_inner(contained, boundary, out_center, out_radius);
    }
}

} // namespace

namespace hrz
{
template<typename T>
BSphere<T> compute_bounding_sphere(gsl::span<const lm::Vector<T, 3>> pts)
{
    if (pts.size() == 0)
    {
        return BSphere<T>{lm::Vector<T, 3>(0, 0, 0), -1};
    }

    T out_radius{};
    lm::Vector<T, 3> out_center;

    if (pts.size() <= 6)
    {
        _compute_exact_bounding_sphere(pts, &out_center, &out_radius);
        return BSphere<T>{out_center, out_radius};
    }

    lm::Vector<T, 3> extremum[6] = {
        pts[0], pts[0], pts[0], pts[0], pts[0], pts[0],
    };

    T proj_extremum[6] = {
        pts[0].x, pts[0].x, pts[0].y, pts[0].y, pts[0].z, pts[0].z,
    };

    // Find extremum points along the primary axes
    for (const auto& p : pts)
    {
        if (p.x > proj_extremum[0])
        {
            proj_extremum[0] = p.x;
            extremum[0] = p;
        }

        if (p.x < proj_extremum[1])
        {
            proj_extremum[1] = p.x;
            extremum[1] = p;
        }

        if (p.y > proj_extremum[2])
        {
            proj_extremum[2] = p.y;
            extremum[2] = p;
        }

        if (p.y < proj_extremum[3])
        {
            proj_extremum[3] = p.y;
            extremum[3] = p;
        }

        if (p.z > proj_extremum[4])
        {
            proj_extremum[4] = p.z;
            extremum[4] = p;
        }

        if (p.z < proj_extremum[5])
        {
            proj_extremum[5] = p.z;
            extremum[5] = p;
        }
    }

    // Compute the exact bounding sphere for the extremum points
    _compute_exact_bounding_sphere(
        gsl::span<const lm::Vector<T, 3>>(extremum), &out_center, &out_radius);

    double max_radius_squared = 0;

    // Extend the bounding sphere to include all points
    for (const auto& p : pts)
    {
        double radius_squared = lm::length2(p - out_center);
        if (radius_squared > max_radius_squared)
        {
            max_radius_squared = radius_squared;
        }
    }

    out_radius = sqrt(max_radius_squared);

    return BSphere<T>{out_center, out_radius};
}

template BSphere<float> compute_bounding_sphere<float>(gsl::span<const lm::vec3> pts);
template BSphere<double> compute_bounding_sphere<double>(gsl::span<const lm::dvec3> pts);

// Based on this https://stackoverflow.com/a/33535438
// I checked the maths so no need to use a PhD's code ;) ;) ;)
template<typename T>
BSphere<T> merge_bounding_spheres(const BSphere<T>& a, const BSphere<T>& b)
{
    if (a.radius < 0) return b;
    if (b.radius < 0) return a;

    lm::Vector<T, 3> diff = b.center - a.center;
    T dist = lm::length(diff);
    T radius_diff = b.radius - a.radius;

    // One sphere inside another, take the largest one
    if (dist <= radius_diff)
    {
        return b;
    }
    else if (dist <= -radius_diff)
    {
        return a;
    }

    // General case here
    T radius = (a.radius + b.radius + dist) / 2;
    lm::Vector<T, 3> center = (T)0.5 * (a.center + b.center) + diff * radius_diff / ((T)2.0 * dist);

    return BSphere<T>{center, radius};
}

template<typename T>
BSphere<T> merge_bounding_spheres(gsl::span<const BSphere<T>> bspheres)
{
    if (bspheres.size() == 0) return BSphere<T>();

    BSphere<T> result = bspheres[0];
    for (size_t i = 1; i < bspheres.size(); ++i)
    {
        result = merge_bounding_spheres(result, bspheres[i]);
    }

    return result;
}

template BSphere<float> merge_bounding_spheres(const BSphere<float>& a, const BSphere<float>& b);
template BSphere<double> merge_bounding_spheres(const BSphere<double>& a, const BSphere<double>& b);
template BSphere<float> merge_bounding_spheres(gsl::span<const BSphere<float>> bspheres);
template BSphere<double> merge_bounding_spheres(gsl::span<const BSphere<double>> bspheres);

} // namespace hrz
