#pragma once

varying vec4 v_color;
varying vec4 v_empty_color;
varying vec3 v_normal;
varying float v_altitude;
varying vec3 v_normal_to_ground;
varying float v_pos_along_line;
varying float v_dash_ratio;
flat varying uint v_invert_gradient_direction;

#ifdef CYLINDER_VISUAL
flat varying uvec2 v_feature_id;
#endif

#ifdef CYLINDER_PICKING
flat varying uint v_feature_index;
#endif
