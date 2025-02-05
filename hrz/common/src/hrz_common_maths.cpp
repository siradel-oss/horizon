#include "hrz_common_maths.h"

namespace hrz
{
double distance_to_triangle(
    const lm::dvec3& a,
    const lm::dvec3& b,
    const lm::dvec3& c,
    const lm::dvec3& p)
{
    auto ap = p - a;
    auto bp = p - b;
    auto cp = p - c;

    auto ab = b - a;
    auto bc = c - b;
    auto ca = a - c;

    auto normal = lm::normalize(lm::cross(ab, c - a));

    if (lm::dot(ap, lm::cross(normal, ab)) >= 0 && lm::dot(bp, lm::cross(normal, bc)) >= 0
        && lm::dot(cp, lm::cross(normal, ca)) >= 0) // Inside triangle
    {
        return std::abs(lm::dot(normal, ap));
    }

    double d0 =
        lm::length2(ab * std::min(std::max(0.0, lm::dot(ab, ap) / lm::length2(ab)), 1.0) - ap);
    double d1 =
        lm::length2(bc * std::min(std::max(0.0, lm::dot(bc, bp) / lm::length2(bc)), 1.0) - bp);
    double d2 =
        lm::length2(ca * std::min(std::max(0.0, lm::dot(ca, cp) / lm::length2(ca)), 1.0) - cp);
    return std::sqrt(std::min(std::min(d0, d1), d2));
}

} // namespace hrz
