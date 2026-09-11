// SPDX-FileCopyrightText: Copyright 2026 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "flat_vectors/interface_common.glsl"

varying vec4 v_color; // sRGB

#ifdef FLAT_POLYGONS_PATTERN
varying vec2 v_uv;
varying float v_in_tile_lat;
flat varying float v_camera_height_pattern_scale_factor;
flat varying float v_reference_lat_scale_factor_offset;
flat varying vec2 v_pattern_sprite_size;
flat varying vec2 v_pattern_sprite_offset;
flat varying mat2 v_pattern_transform;
flat varying vec4 v_pattern_color;
flat varying float v_pattern_color_blend_strength;
#endif
