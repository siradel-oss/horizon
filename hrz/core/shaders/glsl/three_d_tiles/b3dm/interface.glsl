#pragma once

varying vec2 v_uv_0;
varying vec2 v_uv_1;

varying vec4 v_geometry_color_lin;
flat varying vec4 v_feature_color_lin;

#ifdef GLTF_VISUAL
    varying vec3 v_view_pos;
    varying vec3 v_normal;
    varying float v_altitude;
    varying vec3 v_normal_to_ground;
    flat varying uvec2 v_feature_id;
#endif

flat varying uint v_batch_id;

#ifdef B3DM_SELECTION
    flat varying uint v_is_selected;
#endif

#if defined(GLTF_VISUAL) || defined(GLTF_PICKING) || defined(B3DM_SELECTION)
varying vec4 v_overlay_cams_clip_pos[HRZ_S_MAX_OVERLAY_CASCADES];
#endif
