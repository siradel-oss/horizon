#pragma once

varying vec2 v_uv_0;
varying vec2 v_uv_1;

varying vec4 v_geometry_color;
flat varying vec4 v_feature_color;

#ifdef GLTF_VISUAL
    varying vec3 v_view_pos;
    varying vec3 v_normal;
    varying float v_altitude;
    varying vec3 v_normal_to_ground;
#   ifdef GLTF_INSTANCED
        flat varying uvec2 v_feature_id;
#   endif
#endif

#ifdef GLTF_PICKING
#   ifdef GLTF_INSTANCED
        flat varying uint v_object_id;
#   endif
#endif
