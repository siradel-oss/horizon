#pragma once

#include "hrz/protocol/maths/geometry.pb.h"
#include "hrz/protocol/maths/maths.pb.h"

#include <lin_maths.h>

namespace hrz
{
inline lm::vec2 to_lm(const hrz_proto::Vec2f& v)
{
    return lm::vec2(v.x(), v.y());
}

inline lm::ivec2 to_lm(const hrz_proto::Vec2i& v)
{
    return lm::ivec2(v.x(), v.y());
}

inline lm::dvec3 to_lm(const hrz_proto::Vec3d& v)
{
    return lm::dvec3(v.x(), v.y(), v.z());
}

inline lm::vec3 to_lm(const hrz_proto::Vec3f& v)
{
    return lm::vec3(v.x(), v.y(), v.z());
}

inline lm::mat4 to_lm(const hrz_proto::Mat4f& v)
{
    return lm::mat4(
        lm::vec4(v.m00(), v.m01(), v.m02(), v.m03()), lm::vec4(v.m10(), v.m11(), v.m12(), v.m13()),
        lm::vec4(v.m20(), v.m21(), v.m22(), v.m23()), lm::vec4(v.m30(), v.m31(), v.m32(), v.m33()));
}

template<typename T>
lm::Matrix<T, 4> euler_rotation(
    const lm::Vector<T, 3>& rotation,
    hrz_proto::EulerRotationOrder order)
{
    const auto rotation_x = lm::rotation(lm::Vector<T, 3>{1, 0, 0}, rotation.x);
    const auto rotation_y = lm::rotation(lm::Vector<T, 3>{0, 1, 0}, rotation.y);
    const auto rotation_z = lm::rotation(lm::Vector<T, 3>{0, 0, 1}, rotation.z);

    switch (order)
    {
        default:
        case hrz_proto::EULER_ZYX: return rotation_x * rotation_y * rotation_z;
        case hrz_proto::EULER_ZXY: return rotation_y * rotation_x * rotation_z;
        case hrz_proto::EULER_XYZ: return rotation_z * rotation_y * rotation_x;
        case hrz_proto::EULER_XZY: return rotation_y * rotation_z * rotation_x;
        case hrz_proto::EULER_YZX: return rotation_x * rotation_z * rotation_y;
        case hrz_proto::EULER_YXZ: return rotation_z * rotation_x * rotation_y;
    }
}

inline lm::quat to_lm(const hrz_proto::Quat& q, bool normalize = true)
{
    if (!normalize)
    {
        return lm::quat(q.x(), q.y(), q.z(), q.w());
    }

    lm::dvec4 vec(q.x(), q.y(), q.z(), q.w());

    double length2 = lm::length2(vec);
    static const double EPSILON = 0.0000001;
    if (std::abs(length2 - 1) > EPSILON)
    {
        if (length2 < EPSILON)
        {
            vec.x = 0;
            vec.y = 0;
            vec.z = 0;
            vec.w = 1;
        }
        else
        {
            vec = lm::normalize(vec);
        }
    }

    return lm::quat(vec.x, vec.y, vec.z, vec.w);
}

inline lm::vec3 to_lm(hrz_proto::Axis axis)
{
    switch (axis)
    {
        case hrz_proto::Axis::POS_X: return lm::vec3(1, 0, 0);
        case hrz_proto::Axis::NEG_X: return lm::vec3(-1, 0, 0);
        case hrz_proto::Axis::POS_Y: return lm::vec3(0, 1, 0);
        case hrz_proto::Axis::NEG_Y: return lm::vec3(0, -1, 0);
        case hrz_proto::Axis::POS_Z: return lm::vec3(0, 0, 1);
        case hrz_proto::Axis::NEG_Z: return lm::vec3(0, 0, -1);
        default: return lm::vec3();
    }
}

lm::dmat4 to_lm(const hrz_proto::CoordinatesFrame& frame);

lm::dmat4 to_lm(const hrz_proto::Transform& transform);

inline lm::bbox2 to_lm(const hrz_proto::Bbox& bbox)
{
    return lm::bbox2(lm::vec2(bbox.x_min(), bbox.y_min()), lm::vec2(bbox.x_max(), bbox.y_max()));
}

inline lm::dbbox2 to_lm(const hrz_proto::Bboxd& bbox)
{
    return lm::dbbox2(lm::dvec2(bbox.x_min(), bbox.y_min()), lm::dvec2(bbox.x_max(), bbox.y_max()));
}

inline lm::ibbox2 to_lm(const hrz_proto::Bboxi& bbox)
{
    return lm::ibbox2(lm::ivec2(bbox.x_min(), bbox.y_min()), lm::ivec2(bbox.x_max(), bbox.y_max()));
}

inline lm::dbbox3 to_lm(const hrz_proto::Bbox3d& bbox)
{
    return lm::dbbox3(
        lm::dvec3(bbox.x_min(), bbox.y_min(), bbox.z_min()),
        lm::dvec3(bbox.x_max(), bbox.y_max(), bbox.z_max()));
}

inline void to_proto(const lm::dvec3& v, hrz_proto::Vec3d* proto)
{
    proto->set_x(v.x);
    proto->set_y(v.y);
    proto->set_z(v.z);
}

inline void to_proto(const lm::dbbox3& bbox, hrz_proto::Bbox3d* proto)
{
    proto->set_x_min(bbox.min.x);
    proto->set_y_min(bbox.min.y);
    proto->set_z_min(bbox.min.z);
    proto->set_x_max(bbox.max.x);
    proto->set_y_max(bbox.max.y);
    proto->set_z_max(bbox.max.z);
}

inline void to_proto(const lm::bbox2& bbox, hrz_proto::Bbox* proto)
{
    proto->set_x_min(bbox.min.x);
    proto->set_y_min(bbox.min.y);
    proto->set_x_max(bbox.max.x);
    proto->set_y_max(bbox.max.y);
}

inline void to_proto(const lm::dbbox2& bbox, hrz_proto::Bboxd* proto)
{
    proto->set_x_min(bbox.min.x);
    proto->set_y_min(bbox.min.y);
    proto->set_x_max(bbox.max.x);
    proto->set_y_max(bbox.max.y);
}

} // namespace hrz
