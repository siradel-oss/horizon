// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#pragma once

varying vec3 v_uv;
varying vec4 v_color;

#if defined(SYMBOL_TEXT_OUTLINE) || defined(SYMBOL_PICKING) || defined(SYMBOL_SELECTION)
flat varying float v_outline_width;
#endif

#ifdef TEXT_VISUAL
flat varying uvec2 v_feature_id;
#endif

#ifdef TEXT_PICKING
flat varying uint v_feature_index;
#endif
