// SPDX-FileCopyrightText: Copyright 2021 Siradel
// SPDX-License-Identifier: MIT

#pragma once

layout(std140) uniform Control
{
    uvec2 object_reference;
    uint selected_control_point_id;
    bool pick_selected_control_point;
    bool pick_midpoint_control_points;
    highp float control_point_size;
    bool show_midpoint_control_points;
    vec4 control_point_color;
    vec4 control_point_midpoint_color;
    vec4 control_point_selected_color;
} hrz_control;
