#pragma once

#include "hrz/common/shader_defines.h"
#include "hrz/core/render_request.h"
#include "hrz/core/scene_model.h"
#include "hrz/core/scene_path/scene/view_settings_paths.h"

#include <lin_maths.h>
#include <mycelium/renderer.h>

namespace hrz
{

struct ViewshedsSystem;
struct CameraViewInfo;
struct RenderView;

namespace viewsheds
{

static const char* const VIEWSHED_SHADOW_MAP_SAMPLER_NAMES[] = {
    "hrz_viewshed_shadow_map[0]", "hrz_viewshed_shadow_map[1]", "hrz_viewshed_shadow_map[2]",
    "hrz_viewshed_shadow_map[3]", "hrz_viewshed_shadow_map[4]", "hrz_viewshed_shadow_map[5]",
    "hrz_viewshed_shadow_map[6]", "hrz_viewshed_shadow_map[7]",
};

ViewshedsSystem* create();
void destroy(ViewshedsSystem*, RenderView*);

RenderRequest update(ViewshedsSystem* sys, const CameraViewInfo& cam, SceneModel* model);

void draw(ViewshedsSystem*, RenderView*);

bool is_viewshed_enabled(const ViewshedsSystem*);

void notify_model_update(
    ViewshedsSystem*,
    scene_model::UpdateType,
    const scene_model::SceneViewSettingsPath&);

lm::dmat4 get_viewshed_matrix(const ViewshedsSystem*, size_t index);
lm::dvec3 get_viewshed_position_from_main_view(const ViewshedsSystem*, size_t index);
void get_viewshed_colors(const ViewshedsSystem*, size_t index, lm::vec4 colors[2]);
void register_views(
    ViewshedsSystem*,
    RenderView*,
    std::vector<my::Renderer::ViewId>& created_views);
void get_shadow_map_names(const ViewshedsSystem*, const char* names[HRZ_S_VIEWSHED_CNT]);
void init_render(ViewshedsSystem*, RenderView*, const char* input_camera_height);

} // namespace viewsheds
} // namespace hrz
