#pragma once

#include "hrz_core_render.h"
#include "hrz_core_render_request.h"
#include "hrz_core_visibility_constraints.h"

#include <hrz_common_geo.h>
#include <hrz_common_vector_data.h>

#include <cstdint>
#include <optional>

struct mu_Context;

namespace hrz
{
struct ActorRunner;
struct BlobAllocator;
struct CameraViewInfo;
struct JobScheduler;
struct PlanetSurface;
struct ReprRegistry;
struct SelectionSystem;
struct VectorDataLoader;
struct SymbolCullingSystem;
struct AttributionRegistry;

namespace vt
{
/**
 * Contains a tiled dataset of vector features.
 */
struct VectorTiles;

VectorTiles* create(
    uint64_t layer_id,
    uint32_t vector_data_layer,
    hrz_proto::MissingTilePolicy missing_tile_policy,
    bool static_tiles,
    uint32_t picking_id,
    hrz::PlanetSurface* planet,
    VectorDataLoader* vdl,
    ActorRunner* ar);

void destroy(VectorTiles*);

RenderRequest work(
    VectorTiles*,
    BlobAllocator*,
    JobScheduler*,
    ReprRegistry*,
    const SelectionSystem*);

bool is_working(VectorTiles*);

void work_gpu(VectorTiles*, Render*, gsl::span<const RenderViewInfo> views_info);

void draw(
    VectorTiles*,
    Render*,
    const RenderRequest&,
    ReprRegistry*,
    gsl::span<const RenderViewInfo> views_info,
    SymbolCullingSystem* symbol_culling,
    AttributionRegistry*);

void set_bounds(
    VectorTiles*,
    const std::optional<hrz::GeoBounds>& bounds,
    std::optional<uint8_t> min_lod,
    std::optional<uint8_t> max_lod);

void set_clamping(VectorTiles*, const hrz_proto::VectorClamping&);

void set_attributes(VectorTiles*, gsl::span<const hrz_proto::StylingAttributeRef* const>);

void set_max_screen_space_error(VectorTiles*, unsigned int max_screen_space_error);

void set_palettes(VectorTiles*, gsl::span<const hrz_proto::Palette* const>);

std::optional<vector_data::FeatureId> get_feature_id_from_picking_id(
    VectorTiles*,
    uint32_t complementary_id,
    uint32_t object_id);

bool pick_feature(
    VectorTiles*,
    uint32_t complementary_id,
    uint32_t object_id,
    hrz_proto::PickLayerResult*);

void set_style_script(VectorTiles*, std::string_view script);

void add_representation(VectorTiles*, ReprRegistry*, hrz_proto::VectorRepr&& repr);
void set_all_representations(
    VectorTiles*,
    ReprRegistry*,
    gsl::span<const hrz_proto::VectorRepr* const> reprs);
void update_representation(VectorTiles*, ReprRegistry*, size_t index, hrz_proto::VectorRepr&& repr);
void remove_representation(VectorTiles*, ReprRegistry*, size_t index);

void set_special_attributes(
    VectorTiles*,
    std::string_view anchor_z_attribute_name,
    std::string_view anchor_angle_attribute_name,
    std::string_view feature_type_attribute_name);

void set_visible_in(VectorTiles*, SceneViewBitset bitset);

void set_clip_id(VectorTiles*, int32_t clip_id);

void set_lighting(VectorTiles*, const render::LightingSettings&);

void set_rng_seed(VectorTiles*, uint64_t rng_seed);

void dev_ui(VectorTiles*, mu_Context* ctx);

} // namespace vt
} // namespace hrz
