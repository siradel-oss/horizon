// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/common/shader_defines.h"
#include "hrz/core/scene_model.h"
#include "hrz/core/scene_path/scene/view_settings_paths.h"

#include <lin_maths.h>
#include <mycelium/renderer.h>

namespace hrz
{

struct ShadowsSystem;
struct CameraViewInfo;
struct Render;
struct RenderView;

namespace shadows
{

static_assert(HRZ_S_MAX_SUN_CASCADES <= 4, "Too many sun cascades");

static const char* const SUN_SHADOW_MAP_SAMPLER_NAMES[] = {
    "hrz_sun_shadow_map[0]",
    "hrz_sun_shadow_map[1]",
    "hrz_sun_shadow_map[2]",
    "hrz_sun_shadow_map[3]",
};

ShadowsSystem* create(uint32_t cascade_count);
void destroy(ShadowsSystem*, Render*);

void update(
    ShadowsSystem*,
    const CameraViewInfo&,
    const lm::dvec3 ecef_sun_direction,
    lm::vec2 near_far,
    SceneModel*);
void draw(ShadowsSystem*, Render*);

bool are_shadows_enabled(const ShadowsSystem*);
size_t get_cascade_count(const ShadowsSystem*);
double get_shadow_map_far(const ShadowsSystem*);

void notify_model_update(
    ShadowsSystem*,
    scene_model::UpdateType,
    const scene_model::SceneViewSettingsPath&);

lm::dmat4 get_sun_matrix(const ShadowsSystem*, size_t cascade);
void register_views(ShadowsSystem*, Render*, std::vector<my::Renderer::ViewId>& created_views);
void get_shadow_map_names(const ShadowsSystem*, const char* names[HRZ_S_MAX_SUN_CASCADES]);
void init_render(ShadowsSystem*, RenderView*, const char* camera_height_name);

} // namespace shadows
} // namespace hrz
