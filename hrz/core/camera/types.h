#pragma once

#include "hrz/common/maths.h"

#include <lin_maths.h>

namespace hrz
{
struct CameraInfo
{
    float fovy; // In radians
    lm::dvec3 pos;
    lm::dmat4 view;
    lm::dmat4 view_cc;
    lm::dmat4 inv_view;

    constexpr lm::dvec3 forward() const { return -inv_view.z.xyz; }

    constexpr lm::dvec3 up() const { return inv_view.y.xyz; }

    constexpr lm::dvec3 right() const { return inv_view.x.xyz; }

    // This factor can be multiplied by the camera height to obtain a value that "feels" close
    // to the distance from the camera to that focus point we are trying to guess.
    double perceived_distance_factor() const;

    // Returns an approximation of the distance to the focus point based on the camera height to the
    // terrain.
    inline double perceived_distance(double camera_height) const
    {
        return perceived_distance_factor() * camera_height;
    }

    static CameraInfo make_from_fov_and_pose(float fovy, const lm::ddual_quat& pose);
};

struct ViewportInfo
{
    // Ratio from physical to logical pixels.
    float device_pixel_ratio;

    // Size of the full camera in physical pixels.
    lm::vec2 size;
    lm::bbox2 subfrustum;

    double near;
    double far;

    inline float aspect_ratio() const { return size.x / size.y; }

    // Size of the camera view in physical pixels (part of the viewport occupied by the subfrustum)
    inline lm::vec2 subview_size() const { return lm::floor(size * lm::size(subfrustum)); }
};

struct CameraViewInfo
{
    CameraInfo cam;
    ViewportInfo viewport;

    lm::dmat4 proj;
    lm::dmat4 inv_proj;
    lm::dmat4 pv;
    lm::dmat4 inv_pv;
    lm::dmat4 pv_cc;
    lm::dmat4 inv_pv_cc;

    PerspectiveFrustum to_perspective_frustum() const;

    static CameraViewInfo make_from_camera_and_viewport(
        const CameraInfo& cam,
        const ViewportInfo& viewport);
};

struct EnergyHalfTime
{
    double for_user_controls{};
    double for_movements{};
};

} // namespace hrz
