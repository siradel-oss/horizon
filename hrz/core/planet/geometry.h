// SPDX-FileCopyrightText: Copyright 2019 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/common/planet/tile_request.h"
#include "hrz/common/shader_defines.h"
#include "hrz/core/render_request.h"
#include "hrz/core/scene_model.h"
#include "hrz/core/scene_path/scene/view_settings_paths.h"

#include <mycelium/backend.h>
#include <mycelium/render_graph.h>
#include <mycelium/renderer.h>

#include <span>
#include <utility>

namespace hrz
{

struct BlobAllocator;
struct JobScheduler;
struct PlanetGeometry;
struct PickingIdAllocator;
struct Render;
struct RenderView;
struct RenderViewInfo;
struct VectorFlatOverlaySystem;

namespace vtex
{

class ClipmapParams;

}

namespace planet
{

struct GeometryResources
{
    my::UboBinding planet_params;

    my::TextureBinding dtm_indirection;
    my::TextureBinding dtm_atlas;

    my::TextureBinding imagery_indirection[HRZ_S_MAX_IMAGERY_GROUP_COUNT];
    my::TextureBinding imagery_atlas[HRZ_S_MAX_IMAGERY_GROUP_COUNT];
};

struct RequestedTiles
{
    std::span<const planet::RequestedTileCoords> tiles;
    size_t hash;
    bool was_updated;
};

PlanetGeometry* create_geometry();
my::RenderPassId initialize_rendering(PlanetGeometry* geometry, RenderView* render);
void destroy(PlanetGeometry*, JobScheduler*, Render*);

void work(
    PlanetGeometry*,
    BlobAllocator*,
    JobScheduler*,
    SceneModel*,
    const vtex::ClipmapParams*,
    const std::pair<double, double>& dtm_min_max,
    const RenderViewInfo& camera_info,
    double mipmap_bias);

bool is_working(const PlanetGeometry*);

RenderRequest work_gpu(
    PlanetGeometry*,
    Render*,
    my::Renderer::ViewId main_view_id,
    double height_above_terrain);

void draw(PlanetGeometry*, Render*, const GeometryResources&, bool cast_shadows);

// @Todo This should be part of the scene model eventually.
void set_resolution(PlanetGeometry*, double resolution);

void notify_model_update(
    PlanetGeometry*,
    scene_model::UpdateType update_type,
    const scene_model::SceneViewSettingsPath& path);

void request_feedback_render(PlanetGeometry*);

bool is_dtm_enabled(const PlanetGeometry*);

RequestedTiles get_updated_requested_tiles(const PlanetGeometry*);

} // namespace planet
} // namespace hrz
