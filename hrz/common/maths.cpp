#include "hrz/common/maths.h"

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

lm::dmat4 make_frame_transform(const lm::vec3& front, const lm::vec3& up, bool right_handed)
{
    lm::vec3 x = lm::cross(front, up);

    if (!right_handed)
    {
        x = -x;
    }

    if (lm::length2(x) == 0)
    {
        return lm::dmat4::identity();
    }
    else
    {
        return lm::dmat4(
            lm::dvec4(x.x, front.x, up.x, 0.0), lm::dvec4(x.y, front.y, up.y, 0.0),
            lm::dvec4(x.z, front.z, up.z, 0.0), lm::dvec4(0.0, 0.0, 0.0, 1.0));
    }
}

lm::vec3 extract_euler_angles_xyz(const lm::mat4& m)
{
    const float t1 = std::atan2(m.m[2][1], m.m[2][2]);
    const float c2 = std::sqrt(m.m[0][0] * m.m[0][0] + m.m[1][0] * m.m[1][0]);
    const float t2 = std::atan2(-m.m[2][0], c2);
    const float s1 = std::sin(t1);
    const float c1 = std::cos(t1);
    const float t3 = std::atan2(s1 * m.m[0][2] - c1 * m.m[0][1], c1 * m.m[1][1] - s1 * m.m[1][2]);
    return {-t1, -t2, -t3};
}

lm::mat4 euler_angles_xyz_to_mat4(lm::vec3 angles)
{
    const float c1 = cos(-angles.x);
    const float c2 = cos(-angles.y);
    const float c3 = cos(-angles.z);
    const float s1 = sin(-angles.x);
    const float s2 = sin(-angles.y);
    const float s3 = sin(-angles.z);

    lm::mat4 m;
    m.m[0][0] = c2 * c3;
    m.m[0][1] = -c1 * s3 + s1 * s2 * c3;
    m.m[0][2] = s1 * s3 + c1 * s2 * c3;
    m.m[0][3] = 0.0;
    m.m[1][0] = c2 * s3;
    m.m[1][1] = c1 * c3 + s1 * s2 * s3;
    m.m[1][2] = -s1 * c3 + c1 * s2 * s3;
    m.m[1][3] = 0.0;
    m.m[2][0] = -s2;
    m.m[2][1] = s1 * c2;
    m.m[2][2] = c1 * c2;
    m.m[2][3] = 0.0;
    m.m[3][0] = 0.0;
    m.m[3][1] = 0.0;
    m.m[3][2] = 0.0;
    m.m[3][3] = 1.0;
    return m;
}

} // namespace hrz
