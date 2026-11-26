// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/common/horizon_culling.h"
#include "hrz/common/picking_types.h"
#include "hrz/core/render/screen_space.h"
#include "hrz/core/render_request.h"
#include "hrz/core/scene_view_bitset.h"
#include "hrz/core/three_d_tiles/model_cache.h"
#include "hrz/core/three_d_tiles/tile_types.h"
#include "hrz/core/vector/data_loader/data_loader.h"
#include "hrz/fnd/flat_hash_set.h"
#include "hrz/fnd/gen_object_pool.h"
#include "hrz/protocol/3d_model/material.pb.h"
#include "hrz/protocol/attributes/attribute.pb.h"
#include "hrz/protocol/color/palette.pb.h"

#include <lin_maths.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace hrz
{

struct AttributionRegistry;
struct ImageDecoder;
struct Render;
struct RenderViewInfo;

} // namespace hrz

namespace hrz::three_d_tiles
{

struct WorkContext
{
    hrz::AssetsLoader* al{};
    hrz::JobScheduler* js{};
    hrz::BlobAllocator* ba{};
    hrz::ImageDecoder* imgdec{};
    hrz::AttributionRegistry* attributions{};
    std::span<const hrz::RenderViewInfo> views_info;
};

struct TilesetWorkContext : public WorkContext
{
    explicit TilesetWorkContext(const WorkContext& ctx) : WorkContext(ctx) {}

    std::span<const hrz::render::ScreenSpaceError> sses;
    std::span<const hrz::HorizonCuller> horizon_cullers;
    std::span<const std::array<lm::dvec4, 4>> views_planes;
    std::span<const std::array<lm::dvec3, 5>> views_vertices;
    hrz::SceneViewBitset visiting_in;
    bool should_update_priorities = false;
};

template<typename T>
concept ContentSubtileVisitor = requires(
    const T& visitor,
    Tileset& tileset,
    ThreeDTile& tile,
    ThreeDTile::B3dmContent& b3dm,
    ThreeDTile::I3dmContent& i3dm,
    ThreeDTile::PntsContent& pnts) {
    visitor(tileset, tile, b3dm);
    visitor(tileset, tile, i3dm);
    visitor(tileset, tile, pnts);
};

class ThreeDTilesSystem
{
public:
    struct AttributeValue
    {
        std::string_view name;
        hrz_proto::AttributeValue value;
    };

    void init(
        uint8_t system_picking_id,
        hrz::vector_data::VectorDataLoaderChannel&& vector_data_channel);

    void initialize_rendering(hrz::Render* render);

    TilesetH add_tileset(
        std::string_view url,
        const hrz::HttpHeaders& headers,
        hrz::AttributionHandle additional_attribution,
        bool preserve_query_parameters,
        const lm::dmat4& transform,
        double max_screen_space_error,
        double refinement_hysteresis,
        const std::vector<hrz::three_d_tiles::AttributeConfig>& attributes,
        LayerH layer_handle,
        uint64_t global_layer_id,
        bool is_visible,
        int8_t loading_priority,
        int32_t clip_id,
        uint32_t scene_views_bitset,
        uint32_t color_blend_mode,
        float color_blend_strength,
        const hrz::render::LightingSettings& lighting_settings,
        const hrz_proto::LayerVisibilityConstraintList& visibility_constraints,
        bool draw_under_flat_overlays,
        hrz::AssetsLoader* al);

    void set_vector_data_layer_id(TilesetH handle, uint32_t vector_data_layer_id);

    void set_style(
        TilesetH handle,
        std::string_view style_script,
        std::span<const hrz_proto::Palette* const> palettes_proto,
        uint64_t rng_seed);

    void set_visibility(TilesetH handle, bool is_visible, hrz::AssetsLoader* al);

    void set_visibility_constraints(
        TilesetH handle,
        const hrz_proto::LayerVisibilityConstraintList& visibility_constraints);

    bool is_tileset_visible(TilesetH handle);

    void set_max_screen_space_error(TilesetH handle, double max_screen_space_error);

    void set_refinement_hysteresis(TilesetH handle, double refinement_hysteresis);

    void set_loading_priority(TilesetH handle, int8_t loading_priority);

    void set_scene_views_bitset(TilesetH handle, uint32_t bitset);

    // Returns true if the headers change might change the content.
    bool set_http_headers(TilesetH handle, const hrz::HttpHeaders& headers);

    void recreate_all_materials(
        TilesetH handle,
        std::span<const hrz_proto::Material* const> materials);

    void add_material(TilesetH handle, const hrz_proto::Material& material);

    void remove_material(TilesetH handle, size_t index);

    void update_material(TilesetH handle, size_t index, const hrz_proto::Material& material);

    void update_material_palette(
        TilesetH handle,
        size_t index,
        const hrz_proto::NumericPalette& palette);

    bool reload_subtiles_content(TilesetH handle, bool allow_external_tilesets = false);

    void update_appearance(
        TilesetH handle,
        int8_t clip_id,
        const hrz::render::LightingSettings& lighting_settings,
        bool draw_under_flat_overlays);

    void update_material_properties(TilesetH handle, const hrz_proto::MaterialProperties& prps);

    void update_active_materials(
        TilesetH handle,
        std::string_view base,
        std::optional<std::string_view> overlay);

    void add_selected_features(
        TilesetH handle,
        std::span<const hrz::vector_data::FeatureIdHash> feature_ids);

    void clear_selected_features(TilesetH handle);

    void mark_tileset_to_unload(TilesetH handle);

    static void extract_info_from_object_reference(
        const hrz::picking::ObjectReference& ref,
        TilesetH* out_tileset_handle,
        uint32_t* out_tile_index,
        uint32_t* out_batch_id);

    hrz::RenderRequest work(
        hrz::AssetsLoader* al,
        hrz::JobScheduler* js,
        hrz::BlobAllocator* ba,
        hrz::ImageDecoder* imgdec,
        hrz::AttributionRegistry* attributions,
        std::span<const hrz::RenderViewInfo> views_info);

    bool is_working();

    [[nodiscard]]
    hrz::RenderRequest work_gpu(hrz::Render* render, hrz::BlobAllocator* ba);

    void draw(hrz::Render* render, hrz::AttributionRegistry* attributions);

    LayerH get_layer_for_tileset(TilesetH tileset_handle);

    std::string get_url_for_tile(TilesetH tileset_handle, uint32_t tile_index);

    std::optional<hrz::vector_data::FeatureId> get_feature_id(
        TilesetH tileset_handle,
        uint32_t tile_index,
        uint32_t batch_id);

    unsigned int get_attribute_count(TilesetH tileset_handle);

    std::optional<AttributeValue> get_attribute_value(
        TilesetH tileset_handle,
        uint32_t tile_index,
        uint32_t batch_id,
        unsigned int attribute_index);

    void destroy(
        hrz::AssetsLoader* al,
        hrz::JobScheduler* js,
        hrz::BlobAllocator* ba,
        hrz::Render* render);

private:
    using TilesetIndexPool = hrz::GenIndexPool<TilesetH, 1, 13>;
    using TilesetPool = hrz::GenObjectPool<Tileset, TilesetIndexPool, 64>;

    TilesetPool _tilesets;

    hrz::flat_hash_set<TilesetH> _root_tilesets;
    hrz::flat_hash_set<TilesetH> _loading_tilesets;
    hrz::flat_hash_set<TilesetH> _loaded_tilesets;
    hrz::flat_hash_set<TilesetH> _removed_tilesets;

    hrz::model::SharedResources* _model_shared_resources = nullptr;
    hrz::model::SharedResources* _model_shared_resources_instanced = nullptr;

    ModelCache _model_cache;

    // Declared here so it is "cached" an we don't have to recreate it every time
    // we need to pass it to a subtile content method.
    ContentWorkContext _content_work_ctx;

    std::vector<my::ResourceHandle> _removed_resources;

    uint8_t _system_picking_id;

    double _request_priority_timer_ms;

    uint32_t _tile_depth_uniform_id = ~(uint32_t)0;

    hrz::vector_data::VectorDataLoaderChannel _vector_data_channel;

    template<ContentSubtileVisitor VISITOR>
    void visit_tiles(TilesetH handle, const VISITOR& visitor);

    Tileset* get_tileset(TilesetH handle, bool allow_external_tilesets = false);

    void trigger_restyling(Tileset* tileset, bool reload_attributes);

    void cancel_downloads(TilesetH handle, hrz::AssetsLoader* al);

    void unload_subtile(
        ThreeDTile::Subtile& subtile,
        hrz::AssetsLoader* al,
        hrz::JobScheduler* js,
        hrz::BlobAllocator* ba);

    void unload_tile(
        Tileset* tileset,
        ThreeDTile& tile,
        hrz::AssetsLoader* al,
        hrz::JobScheduler* js,
        hrz::BlobAllocator* ba);

    void unload_tileset(
        TilesetH handle,
        hrz::AssetsLoader* al,
        hrz::JobScheduler* js,
        hrz::BlobAllocator* ba);

    static void start_loading_tile_data(
        Tileset* tileset,
        ThreeDTile* tile,
        const TilesetWorkContext& ctx);

    static void load_tile(
        Tileset* tileset,
        uint32_t tile_index,
        ThreeDTile& tile,
        const TilesetWorkContext& ctx);

    hrz::RenderRequest work_root_tile_refinement(Tileset* tileset, const TilesetWorkContext& ctx);

    static hrz::picking::ObjectReference make_object_reference(
        uint8_t system_picking_id,
        TilesetH tileset_handle,
        uint32_t tile_index);

    hrz::RenderRequest work_tile_load_status(
        Tileset* tileset,
        TilesetH tileset_handle,
        uint32_t tile_index,
        ThreeDTile& tile,
        const TilesetWorkContext& ctx);

    hrz::RenderRequest work_loading_tile_content(
        Tileset* tileset,
        ThreeDTile& tile,
        const TilesetWorkContext& ctx);

    hrz::RenderRequest work_tile_styling(
        const Tileset* tileset,
        ThreeDTile& tile,
        const TilesetWorkContext& ctx);

    bool unload_tile_if_unused(Tileset* tileset, ThreeDTile& tile, const TilesetWorkContext& ctx);

    hrz::RenderRequest start_rendering_children(
        Tileset* tileset,
        uint32_t tile_index,
        ThreeDTile& tile,
        hrz::SceneViewBitset visiting_in);

    hrz::RenderRequest stop_rendering_children(
        Tileset* tileset,
        uint32_t tile_index,
        ThreeDTile& tile,
        hrz::SceneViewBitset visiting_in);

    bool load_external_tileset(
        Tileset* tileset,
        uint32_t tile_index,
        ThreeDTile& tile,
        ThreeDTile::SubtileIdentification* ident,
        const hrz::blobs::BlobHandle& tile_data_blob,
        const WorkContext& ctx);

    bool decode_tile_data(
        Tileset* tileset,
        uint32_t tile_index,
        ThreeDTile& tile,
        ThreeDTile::SubtileIdentification* subtile_ident,
        const hrz::blobs::BlobHandle& tile_data_blob,
        std::string_view tile_mime_type,
        const WorkContext& ctx);

    hrz::RenderRequest update_visibility_constraints(
        std::span<const hrz::RenderViewInfo> views_info);

    void work_loading_tilesets(
        hrz::AssetsLoader* al,
        hrz::JobScheduler* js,
        hrz::BlobAllocator* ba);

    // Requests the attribute values when possible, and styles the subtile once they arrive.
    hrz::RenderRequest work_waiting_for_attributes(
        const TilesetConfig& config,
        const ThreeDTile& tile,
        ThreeDTile::Subtile& subtile,
        hrz::JobScheduler* js);

    void release_attribute_request(ThreeDTile::Subtile& subtile);

    // The request is made again once the subtile is worked on.
    void cancel_attribute_request(ThreeDTile::Subtile& subtile);

    // Falls back to the default style if the job can't be started.
    hrz::RenderRequest start_style_job(
        const TilesetConfig& config,
        const ThreeDTile& tile,
        ThreeDTile::Subtile& subtile,
        hrz::JobScheduler* js);

    // Start or restart the styling process.
    hrz::RenderRequest start_styling_process(
        const TilesetConfig& config,
        ThreeDTile& tile,
        ThreeDTile::Subtile& subtile,
        bool reload_attribute_values,
        const TilesetWorkContext& ctx);

    void work_loading_tile(
        TilesetH tileset_handle,
        Tileset* tileset,
        uint32_t tile_index,
        ThreeDTile* tile,
        const TilesetWorkContext& ctx);

    hrz::RenderRequest work_subtile_styling(
        const Tileset* tileset,
        ThreeDTile& tile,
        ThreeDTile::Subtile& subtile,
        const TilesetWorkContext& ctx);

    void update_external_tilesets_render_status(ThreeDTile& tile);

    hrz::RenderRequest on_render_status_set(
        ThreeDTile& tile,
        hrz::SceneViewBitset render_self_in_before,
        hrz::SceneViewBitset render_children_in_before);

    // When the status changes, these also propagate it to the tile's external tilesets
    // and request a redraw.
    hrz::RenderRequest set_render_self_in(ThreeDTile& tile, hrz::SceneViewBitset views);
    hrz::RenderRequest set_render_children_in(ThreeDTile& tile, hrz::SceneViewBitset views);
    hrz::RenderRequest set_render_self_and_children_in(
        ThreeDTile& tile,
        hrz::SceneViewBitset views);
    hrz::RenderRequest set_render_none_in(ThreeDTile& tile, hrz::SceneViewBitset views);

    // Second value is whether this tile should be erased from the active list
    // Loading and styling work, done for all active tiles before the traversal.
    hrz::RenderRequest work_tile_content(
        Tileset* tileset,
        TilesetH tileset_handle,
        uint32_t tile_index,
        const TilesetWorkContext& ctx);

    std::pair<hrz::RenderRequest, bool> work_tile_traversal(
        Tileset* tileset,
        uint32_t tile_index,
        std::vector<uint32_t>* tiles_to_refine,
        const TilesetWorkContext& ctx);

    hrz::RenderRequest work_loaded_tileset(
        Tileset* tileset,
        TilesetH tileset_handle,
        const TilesetWorkContext& ctx);

    hrz::RenderRequest work_loaded_tilesets(const WorkContext& ctx);

    struct AttributeRequestTarget
    {
        Tileset* tileset = nullptr;
        ThreeDTile* tile = nullptr;
        ThreeDTile::Subtile* subtile = nullptr;
    };

    Tileset* find_tileset_for_layer_request(uint64_t vector_data_layer_request_id);

    AttributeRequestTarget find_subtile_for_attribute_request(uint64_t attribute_request_id);

    void apply_attribute_data(
        Tileset& tileset,
        ThreeDTile& tile,
        ThreeDTile::Subtile& subtile,
        hrz::vector_data::messages::DataUpdate& message,
        hrz::JobScheduler* js);

    void on_attribute_reply(
        Tileset& tileset,
        ThreeDTile& tile,
        ThreeDTile::Subtile& subtile,
        hrz::JobScheduler* js);

    void work_attribute_messages(const WorkContext& ctx);

    ContentRenderContext make_content_render_context(hrz::Render* render) const;
};

template<ContentSubtileVisitor VISITOR>
void ThreeDTilesSystem::visit_tiles(TilesetH handle, const VISITOR& visitor)
{
    auto tileset = _tilesets.get_object(handle);
    if (tileset == nullptr) return;

    for (auto& tile : tileset->tiles)
    {
        for (auto& subtile : tile.subtiles)
        {
            std::visit(
                hrz::overload{
                    [&](ThreeDTile::TilesetContent& content)
                    { visit_tiles<VISITOR>(content.tileset_handle, visitor); },
                    [&](ContentSubtile auto& content) { visitor(*tileset, tile, content); },
                    [](std::monostate) {}
                },
                subtile.content);
        }
    }
}

} // namespace hrz::three_d_tiles
