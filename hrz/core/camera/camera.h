#pragma once

#include "hrz/common/geo.h"
#include "hrz/core/camera/types.h"
#include "hrz/core/scene_model.h"
#include "hrz/core/scene_path/camera/settings_paths.h"
#include "hrz/core/scene_view_bitset.h"
#include "hrz/protocol/camera/movement.pb.h"
#include "hrz/protocol/camera/notification.pb.h"
#include "hrz/protocol/camera/service.pb.h"
#include "hrz/protocol/camera/transitions.pb.h"
#include "hrz/protocol/camera/viewpoints.pb.h"

struct mu_Context;

namespace hrz
{

struct PlanetSurface;
struct PickingSystem;
struct ViewportEvent;

} // namespace hrz

namespace hrz::camera
{

class Camera
{
public:
    virtual ~Camera() = default;

    // @Note We don't return a boolean to indicate whether the event was consumed
    // because it's unnecessary since controling the camera is always the last
    // possible way to handle an event.
    // And this simplifies the camera implementation :)
    //      -slerouzic, 2023-04-12
    virtual void handle_event(
        const ViewportEvent&,
        hrz_proto::SceneViewIndex,
        const CameraViewInfo&) = 0;

    virtual void notify_model_update(
        scene_model::UpdateType,
        const scene_model::CameraSettingsPath&) = 0;

    // Returns whether the camera has moved or not
    virtual bool work(
        SceneModel*,
        const ViewportInfo&,
        double height_above_terrain,
        const std::array<PickingSystem*, hrz::SCENE_VIEW_COUNT>&,
        PlanetSurface*,
        const std::function<void(std::span<const hrz_proto::CameraNotification>)>&
            notifications_cb) = 0;

    virtual const CameraInfo& get_info() const = 0;

    virtual hrz_proto::Pose get_pose() const = 0;
    virtual hrz_proto::AngularViewpoint get_angular_viewpoint() const = 0;
    virtual hrz_proto::PositionalViewpoint get_positional_viewpoint() const = 0;
    virtual hrz_proto::GeographicViewPolygon get_view_polygon(const ViewportInfo&) const = 0;
    virtual hrz_proto::GeographicBounds get_view_box(const ViewportInfo&) const = 0;

    virtual bool geo_to_screen(
        const GeoPosition3&,
        const ViewportInfo&,
        lm::vec2* position,
        bool* below_horizon) const = 0;

    virtual void reset_north(const hrz_proto::ResetNorthParams&) = 0;

    virtual void go_to_orbit(
        const hrz_proto::OrbitCameraTransition&,
        double fovy_rad,
        double aspect_ratio) = 0;

    virtual void go_to_fixed_position(const hrz_proto::FixedPositionCameraTransition&) = 0;

    virtual void go_to_fixed_target(const hrz_proto::FixedTargetCameraTransition&) = 0;

    virtual void move(
        const hrz_proto::CameraMovement&,
        hrz_proto::SceneViewIndex view_index,
        const ViewportInfo&) = 0;

    virtual void begin_move(
        const hrz_proto::CameraMovement&,
        hrz_proto::SceneViewIndex view_index,
        const ViewportInfo&) = 0;

    virtual void end_move(
        const hrz_proto::CameraMovement&,
        hrz_proto::SceneViewIndex view_index,
        const ViewportInfo&) = 0;

    virtual void dev_ui(mu_Context* ctx) const = 0;
};

Camera* create(
    hrz_proto::CameraIndex,
    float fov,
    double user_controls_inertia,
    double movements_inertia,
    double min_height_above_terrain,
    double terrain_collision_inertia);

void destroy(Camera*);

} // namespace hrz::camera
