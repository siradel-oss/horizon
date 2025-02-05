#include "camera/hrz_core_camera_manipulator.h"

namespace hrz::camera
{

void CameraManipulator::actually_cancel_picking(
    const std::array<PickingSystem*, hrz::SCENE_VIEW_COUNT>& picking)
{
    if (_context._picking_to_cancel.empty())
    {
        for (const auto& ticket : _context._picking_to_cancel)
        {
            PickingSystem* ps = picking[ticket.view];
            if (ps && ticket.system_ptr == (uintptr_t)ps)
            {
                picking::cancel(ps, ticket.ticket);
            }
        }
        _context._picking_to_cancel.clear();
    }
}

lm::ddual_quat CameraManipulator::work_controller(
    double dt,
    double height_above_terrain,
    const std::array<PickingSystem*, hrz::SCENE_VIEW_COUNT>& picking)
{
    if (_controller)
    {
        return _controller->work(
            context(), dt, height_above_terrain, should_keep_bearing(), picking);
    }
    else
    {
        return {};
    }
}

lm::ddual_quat CameraManipulator::work(
    double dt,
    float fovy,
    const ViewportInfo& viewport,
    double height_above_terrain,
    const std::array<PickingSystem*, hrz::SCENE_VIEW_COUNT>& picking,
    const std::function<void(gsl::span<const hrz_proto::CameraNotification>)>& notifications_cb)
{
    if (_delayed_pose)
    {
        set_pose(_delayed_pose.value());
        _delayed_pose.reset();
    }

    lm::ddual_quat new_pose =
        work_inner(dt, fovy, viewport, height_above_terrain, picking, notifications_cb);

    if (is_ready_to_handle_movements() && !_queued_movements.empty())
    {
        for (const auto& movement : _queued_movements)
        {
            auto new_cam_info = CameraInfo::make_from_fov_and_pose(fovy, new_pose);
            auto new_cam_view_info =
                CameraViewInfo::make_from_camera_and_viewport(new_cam_info, viewport);
            handle_movement(movement.type, movement.movement, movement.view, new_cam_view_info);
        }
        _queued_movements.clear();
    }

    actually_cancel_picking(picking);
    if (!_context._notifications.empty())
    {
        notifications_cb(_context._notifications);
        _context._notifications.clear();
    }

    return new_pose;
}

void CameraManipulator::destroy(const std::array<PickingSystem*, hrz::SCENE_VIEW_COUNT>& picking)
{
    if (_controller)
    {
        _controller->on_end(&_context);
    }
    actually_cancel_picking(picking);
}

void CameraManipulator::update_energy_half_time(const EnergyHalfTime& e)
{
    if (_controller)
    {
        _controller->update_energy_half_time(e);
    }
}

void CameraManipulator::update_terrain_settings(
    double min_height_above_terrain,
    double terrain_collision_inertia)
{
    if (_controller)
    {
        _controller->update_terrain_settings(min_height_above_terrain, terrain_collision_inertia);
    }
}

// See in hrz_core_camera_controller.h why we need this
hrz_proto::CameraNotification* add_notification(CameraManipulatorContext* ctx)
{
    return ctx->add_notification();
}

} // namespace hrz::camera
