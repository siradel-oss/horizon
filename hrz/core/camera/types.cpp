#include "hrz/core/camera/types.h"

#include "hrz/common/geo.h"

hrz::CameraInfo hrz::CameraInfo::make_from_fov_and_pose(float fovy, const lm::ddual_quat& pose)
{
    lm::dmat4 view_to_world = transform_matrix(pose);
    lm::dmat4 world_to_view = transform_matrix(lm::inverse_assuming_unit(pose));

    CameraInfo baked_info;
    baked_info.fovy = fovy;
    baked_info.view = world_to_view;
    baked_info.inv_view = view_to_world;
    baked_info.pos = lm::extract_translation(pose);
    baked_info.view_cc = baked_info.view;
    baked_info.view_cc.w.xyz = lm::dvec3(0);

    return baked_info;
}

double hrz::CameraInfo::perceived_distance_factor() const
{
    auto to_camera = lm::normalize(pos);
    auto camera_forward = lm::normalize(forward());
    auto tilt = lm::PI - std::acos(lm::dot(to_camera, camera_forward));

    hrz::GeoPosition3 geo = hrz::ecef_to_geo3(pos);
    // The tilt of a camera that would be looking at the horizon (from where the current camera is).
    auto horizon_tilt = -acos(HRZ_S_EARTH_RADIUS / (HRZ_S_EARTH_RADIUS + geo.alt)) + lm::PI / 2.0;

    // We try to guess where the user is looking at based on the tilt of the camera
    // (which will tell us on which part of the scene to put the focus on):
    // - If the horizon is above the view, then we just use the normal tilt of the camera.
    // - If the horizon is in view, then we use the tilt between the tilt of the lower plane of the
    //   camera frustum and the horizon.
    auto lower_frustum_tilt = std::min(tilt - fovy / 2.0, horizon_tilt);
    auto upper_frustum_tilt = std::min(tilt + fovy / 2.0, horizon_tilt);
    auto corrected_tilt = (lower_frustum_tilt + upper_frustum_tilt) / 2.0;

    return 1.0 / cos(clamp(corrected_tilt, 0.0, lm::PI / 2.0 * 0.95));
}

hrz::PerspectiveFrustum hrz::CameraViewInfo::to_perspective_frustum() const
{
    PerspectiveFrustum frustum;
    frustum.fovy = cam.fovy;
    frustum.aspect_ratio = viewport.aspect_ratio();
    frustum.near = viewport.near;
    frustum.far = viewport.far;
    frustum.subfrustum = viewport.subfrustum;
    return frustum;
}

hrz::CameraViewInfo hrz::CameraViewInfo::make_from_camera_and_viewport(
    const CameraInfo& cam,
    const ViewportInfo& viewport)
{
    CameraViewInfo info;
    info.cam = cam;
    info.viewport = viewport;

    info.proj = lm::perspective_subfrustum_opengl<double>(
        cam.fovy, viewport.aspect_ratio(), viewport.subfrustum.min.x * 2 - 1,
        viewport.subfrustum.max.x * 2 - 1, viewport.subfrustum.min.y * 2 - 1,
        viewport.subfrustum.max.y * 2 - 1, viewport.near, viewport.far);

    info.pv = info.proj * info.cam.view;
    info.pv_cc = info.proj * info.cam.view_cc;

    info.inv_proj = lm::inverse(info.proj);
    info.inv_pv = lm::inverse(info.pv);
    info.inv_pv_cc = lm::inverse(info.pv_cc);

    return info;
}
