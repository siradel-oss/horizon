// SPDX-FileCopyrightText: Copyright 2019 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#if defined(PLANET_VISUAL) || defined(PLANET_FEEDBACK)
varying vec2 v_partial_wmerc_pos;
flat varying vec2 v_base_wmerc_pos;
varying vec3 v_view_pos;
#endif

#if defined(PLANET_VISUAL) || defined(PLANET_PICKING) || defined(PLANET_SELECTION)
varying vec4 v_overlay_cams_clip_pos[HRZ_S_MAX_OVERLAY_CASCADES];
#endif

#ifdef PLANET_VISUAL
varying vec4 v_normal_altitude; // xyz: normal, w: altitude
varying vec3 v_view_normal_to_sun;
#endif
