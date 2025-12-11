#pragma once

#include "hrz/core/render_request.h"
#include "hrz/core/scene_model.h"
#include "hrz/core/scene_path/scene/view_settings_paths.h"

#include <lin_maths.h>

namespace hrz
{
struct SkySystem;
struct Render;
struct RenderView;
struct CameraViewInfo;
struct SceneModel;
struct FrameUniformData;

namespace sky
{
static const char* const SUN_COLOR_SAMPLER_NAME = "hrz_sun_color_lut";

SkySystem* create();
void destroy(SkySystem*, Render*);

void notify_model_update(
    SkySystem*,
    scene_model::UpdateType,
    const scene_model::SceneViewSettingsPath&);

void init_render_precompute(SkySystem*, RenderView*);
void init_render_background(SkySystem*, RenderView*, const char* input_color);
void init_render_world(SkySystem*, RenderView*, const char* input_color, const char* input_depth);

RenderRequest update(SkySystem*, const CameraViewInfo&, SceneModel*);
RenderRequest work_gpu(SkySystem*, Render*);

const char* get_background_color_output(const SkySystem*);
const char* get_world_color_output(const SkySystem*);
const char* get_world_depth_output(const SkySystem*);
const char* get_sun_color_output(const SkySystem*);

void fill_frame_uniform_data(const SkySystem*, FrameUniformData*);
lm::dvec3 get_ecef_sun_direction(const SkySystem*);

bool is_dynamic_sun_lighting_enabled(const SkySystem*);
bool is_dynamic_ambient_lighting_enabled(const SkySystem*);

} // namespace sky
} // namespace hrz
