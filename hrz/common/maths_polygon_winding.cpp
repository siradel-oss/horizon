#include "hrz/common/geo.h"
#include "hrz/common/maths.h"

#include <algorithm>

// See https://en.wikipedia.org/wiki/Curve_orientation

namespace
{

template<typename T>
bool compare(const T& a, const T& b)
{
    if (a.x != b.x) return a.x < b.x;
    return a.y < b.y;
}

template<>
bool compare<hrz::GeoPosition2>(const hrz::GeoPosition2& a, const hrz::GeoPosition2& b)
{
    if (a.lat != b.lat) return a.lat < b.lat;
    return a.lon < b.lon;
}

template<typename T>
double det(const T& a, const T& b, const T& c)
{
    return (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
}

template<>
double det<hrz::GeoPosition2>(
    const hrz::GeoPosition2& a,
    const hrz::GeoPosition2& b,
    const hrz::GeoPosition2& c)
{
    return (b.lat - a.lat) * (c.lon - a.lon) - (c.lat - a.lat) * (b.lon - a.lon);
}

} // namespace

namespace hrz
{

template<typename T>
bool is_clockwise(std::span<const T> pts)
{
    if (pts.size() < 3)
    {
        return false;
    }

    auto pivot_it = std::ranges::min_element(pts, compare<T>);

    size_t b_index = std::distance(pts.begin(), pivot_it);
    size_t a_index = (b_index > 0) ? b_index - 1 : pts.size() - 1;
    size_t c_index = (b_index < pts.size() - 1) ? b_index + 1 : 0;

    auto a = pts[a_index];
    auto b = pts[b_index];
    auto c = pts[c_index];

    return det(a, b, c) < 0;
}

template bool is_clockwise<lm::vec3>(std::span<const lm::vec3> pts);
template bool is_clockwise<lm::vec2>(std::span<const lm::vec2> pts);
template bool is_clockwise<lm::dvec3>(std::span<const lm::dvec3> pts);
template bool is_clockwise<lm::dvec2>(std::span<const lm::dvec2> pts);
template bool is_clockwise<hrz::GeoPosition2>(std::span<const hrz::GeoPosition2> pts);

} // namespace hrz
