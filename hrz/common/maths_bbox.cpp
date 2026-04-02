#include "hrz/common/maths.h"
#include "hrz/fnd/flat_hash_set.h"

namespace hrz
{

template<typename T>
OrientedBBox2<T> compute_minimum_bbox(std::span<const lm::Vector<T, 2>> pts)
{
    std::vector<lm::Vector<T, 2>> hull;
    compute_convex_hull(pts, hull);

    if (hull.size() == 0)
    {
        return OrientedBBox2<T>();
    }
    else if (hull.size() == 1)
    {
        OrientedBBox2<T> obb;
        obb.center = hull[0];
        obb.angle = 0;
        obb.half_width = 0;
        obb.half_height = 0;
        return obb;
    }

    hrz::flat_hash_set<T> angles;

    // Enumerate the possible angles.
    // We modulo them all by 90° because of symmetry.
    // Also we quantize the possible angles because it won't make a huge
    // difference, and it will highly reduce the number of angles to test.
    for (size_t i = 0; i < hull.size() - 1; ++i)
    {
        lm::Vector<T, 2> dir = hull[i + 1] - hull[i];
        T angle = std::fmod(lm::degrees(std::atan2(dir.y, dir.x)), (T)90);
        if (angle < 0)
        {
            angle += (T)90;
        }

        angle = lm::radians(std::floor(angle));

        if (!std::isnan(angle))
        {
            angles.insert(angle);
        }
    }

    OrientedBBox2<T> obb;
    T min_area = std::numeric_limits<T>::max();

    for (T angle : angles)
    {
        lm::Matrix<T, 2> rotation{
            {std::cos(angle), -std::sin(angle)},
            {std::sin(angle), std::cos(angle)}
        };

        lm::Bbox<T, 2> bbox(
            {std::numeric_limits<T>::max(), std::numeric_limits<T>::max()},
            {std::numeric_limits<T>::lowest(), std::numeric_limits<T>::lowest()});

        for (const lm::Vector<T, 2>& pt : pts)
        {
            bbox = lm::expand(bbox, rotation * pt);
        }

        T area = lm::area(bbox);
        if (area < min_area)
        {
            lm::Vector<T, 2> size = lm::size(bbox);
            obb.center = lm::transpose(rotation) * lm::center(bbox);
            obb.half_width = size.x / 2;
            obb.half_height = size.y / 2;
            obb.angle = angle;
            min_area = area;
        }
    }

    return obb;
}

template OrientedBBox2<float> compute_minimum_bbox(std::span<const lm::vec2> pts);
template OrientedBBox2<double> compute_minimum_bbox(std::span<const lm::dvec2> pts);

} // namespace hrz
