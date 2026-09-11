// SPDX-FileCopyrightText: Copyright 2019 Siradel
// SPDX-License-Identifier: MIT

#pragma once

varying vec4 v_color;
varying vec2 v_uv;
flat varying vec2 v_frame;

#ifdef IMPOSTOR_VISUAL
flat varying uvec2 v_feature_id;
varying vec3 v_normal_to_ground;
varying float v_altitude;
// This is a mat3 split into three separate vec3. Passing and using the mat3 directly
// from vertex to fragment shader may invalidate its value because of the storage layout rules
// followed by varyings.
flat varying vec3 v_impostor_to_view_x;
flat varying vec3 v_impostor_to_view_y;
flat varying vec3 v_impostor_to_view_z;
#endif

#ifdef IMPOSTOR_PICKING
flat varying uint v_object_id;
#endif
