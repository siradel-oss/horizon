#pragma once

varying vec4 v_color;

#ifdef POINT_CLOUD_VISUAL
    varying vec3 v_normal;
    varying float v_altitude;
    varying vec3 v_normal_to_ground;
    flat varying uvec2 v_feature_id;
#endif

#ifdef POINT_CLOUD_PICKING
    flat varying uint v_batch_id;
#endif
