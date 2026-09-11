// SPDX-FileCopyrightText: Copyright 2024 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include <lin_maths.h>

namespace hrz::camera
{

lm::ddual_quat push_camera_position_above_terrain(
    const lm::ddual_quat& pose,
    double height_above_terrain,
    double target_height_above_terrain,
    double collision_inertia,
    double dt);

struct PushedCameraPositionForTarget
{
    lm::ddual_quat pose;
    double tilt;
};

PushedCameraPositionForTarget push_camera_position_above_terrain_with_target(
    const lm::ddual_quat& pose,
    double height_above_terrain,
    double target_height_above_terrain,
    const lm::dvec3& target,
    double collision_inertia,
    double dt);

} // namespace hrz::camera
