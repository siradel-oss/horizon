#pragma once

#ifdef EXTRUDED_VISUAL
varying vec4 v_color;
varying vec3 v_normal;
varying float v_altitude;
varying vec3 v_normal_to_ground;

flat varying uvec2 v_feature_id;
#endif

#ifdef EXTRUDED_PICKING
flat varying uint v_feature_index;
#endif
