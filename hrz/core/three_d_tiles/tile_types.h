// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/common/metrics.h"
#include "hrz/common/palette.h"
#include "hrz/common/style/flat_ast.h"
#include "hrz/common/three_d_tiles.h"
#include "hrz/common/vector_data/feature_ids.h"
#include "hrz/common/vector_data/packed_attribute_values.h"
#include "hrz/core/assets_loader/assets_loader.h"
#include "hrz/core/base_url.h"
#include "hrz/core/jobs/jobs_tickets.h"
#include "hrz/core/loading_priorities.h"
#include "hrz/core/model/materials_manager.h"
#include "hrz/core/model/model.h"
#include "hrz/core/point_cloud.h"
#include "hrz/core/render/lighting_settings.h"
#include "hrz/core/render_request.h"
#include "hrz/core/visibility_constraints.h"
#include "hrz/fnd/defines.h"
#include "hrz/fnd/flat_hash_set.h"
#include "hrz/fnd/observed.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <concepts>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace hrz::three_d_tiles
{

using LayerH = uint32_t;
using TilesetH = uint32_t;

// Request IDs sent to the vector data loader, packed as:
// tileset handle (32 bits) | tile index (24 bits) | subtile index (8 bits).
// Layer requests set all the low 32 bits, so that value is reserved.
struct VectorDataRequestId
{
    static constexpr uint32_t TILE_INDEX_BITS = 24;
    static constexpr uint32_t SUBTILE_INDEX_BITS = 8;
    static constexpr uint32_t TILE_INDEX_MASK = (1U << TILE_INDEX_BITS) - 1;
    static constexpr uint32_t SUBTILE_INDEX_MASK = (1U << SUBTILE_INDEX_BITS) - 1;

    TilesetH tileset_handle = 0;
    uint32_t tile_index = 0;
    uint32_t subtile_index = 0;

    static constexpr uint64_t for_layer(TilesetH tileset_handle)
    {
        return ((uint64_t)tileset_handle << 32) | 0xffffffffU;
    }

    static constexpr TilesetH tileset_handle_of(uint64_t request_id)
    {
        return (TilesetH)(request_id >> 32);
    }

    constexpr uint64_t encode() const
    {
        assert(tile_index <= TILE_INDEX_MASK);
        assert(subtile_index <= SUBTILE_INDEX_MASK);
        assert(tile_index != TILE_INDEX_MASK || subtile_index != SUBTILE_INDEX_MASK);

        return ((uint64_t)tileset_handle << 32) | ((uint64_t)tile_index << SUBTILE_INDEX_BITS)
            | subtile_index;
    }

    static constexpr VectorDataRequestId decode(uint64_t request_id)
    {
        return {
            .tileset_handle = tileset_handle_of(request_id),
            .tile_index = (uint32_t)(request_id >> SUBTILE_INDEX_BITS) & TILE_INDEX_MASK,
            .subtile_index = (uint32_t)request_id & SUBTILE_INDEX_MASK,
        };
    }
};

struct TilesetConfig;
struct Tileset;
class ModelCache;

struct ContentWorkContext
{
    hrz::AssetsLoader* al{};
    hrz::JobScheduler* js{};
    hrz::BlobAllocator* ba{};
    hrz::ImageDecoder* imgdec{};
    hrz::AttributionRegistry* attributions{};
    ModelCache* model_cache{};
};

struct ContentRenderContext
{
    hrz::Render* render{};
    hrz::model::SharedResources* model_shared_resources{};
    hrz::model::SharedResources* model_shared_resources_instanced{};
};

inline lm::dmat4 get_axis_swap_transform(hrz::three_d_tiles::GltfUpAxis gltf_up_axis)
{
    // Y and Z axes must be swapped, see
    // https://github.com/CesiumGS/3d-tiles/tree/master/specification#y-up-to-z-up
    static const lm::dmat4 s_axes_transform_y = lm::dmat4(
        lm::dvec4(1, 0, 0, 0), lm::dvec4(0, 0, 1, 0), lm::dvec4(0, -1, 0, 0),
        lm::dvec4(0, 0, 0, 1));

    // But if gltfUpAxis is X, then X and Y axes must be swapped.
    // The gltfUpAxis property is not part of 3D Tiles 1.0, but unfortunately many
    // datasets rely on it, and Cesium still supports it, even the newer variants
    // have added support for it.
    // So it is de-facto part of the format.
    // See https://github.com/CesiumGS/cesium-unreal/issues/315
    static const lm::dmat4 s_axes_transform_x = lm::dmat4(
        lm::dvec4(0, 0, 1, 0), lm::dvec4(0, 1, 0, 0), lm::dvec4(-1, 0, 0, 0),
        lm::dvec4(0, 0, 0, 1));

    static const lm::dmat4 s_axes_transform_z = lm::dmat4::identity();

    switch (gltf_up_axis)
    {
        using enum hrz::three_d_tiles::GltfUpAxis;
        case X: return s_axes_transform_x;
        case Y: return s_axes_transform_y;
        case Z: return s_axes_transform_z;
        default: assert(false && "Unhandled case"); return s_axes_transform_y;
    }
}

struct ThreeDTile
{
    enum class LoadStatus
    {
        UNLOADED,
        LOAD_WHEN_INSIDE_VIEWER_VOLUME,
        LOADING_TILE,
        LOADING_CONTENT,
        LOADED,
        RENDERABLE,
        ERROR
    };

    enum class StylingStatus
    {
        WAITING_FOR_ATTRIBUTES, // See `vector_data_attribute_status` for the details.
        WAITING_FOR_JOB,
        WAITING_FOR_RESOURCE_UPDATE,
        IDLE
    };

    enum class ContentLoadStatusUpdate
    {
        STILL_LOADING,
        ERROR,
        LOADED,
    };

    struct Subtile;

    struct SubtileIdentification
    {
        hrz::picking::ObjectReference object_reference;   // tileset + tile (+ batch index later)
        hrz::picking::FeatureReference feature_reference; // layer ID (+ feature ID later)
        uint32_t batch_id_offset{};
    };

    class B3dmContent
    {
        hrz::model::ModelPrototype* _prototype = nullptr;
        std::optional<hrz::model::BatchedModelGeometryH> _geometry;
        hrz::model::MaterialsManager<hrz::model::BatchedModelMaterialManagerTraits> _materials;

        hrz::model::DrawProperties _draw_prps;

        hrz::picking::ObjectReference _object_reference;
        hrz::picking::FeatureReference _feature_reference;

        [[nodiscard]]
        static bool decode_b3dm_feature_table(
            Subtile* subtile,
            std::span<const std::byte> feature_table_json_data,
            const hrz::blobs::BlobHandle& feature_table_bin_blob);

        static void load_gltf_blob(
            const ContentWorkContext& ctx,
            const Tileset& tileset,
            const ThreeDTile& tile,
            Subtile& subtile,
            const hrz::blobs::BlobHandle& gltf_blob,
            size_t gltf_offset,
            const ThreeDTile::SubtileIdentification& ident);

    public:
        static std::optional<ThreeDTile::Subtile> load_content(
            const ContentWorkContext& ctx,
            const Tileset& tileset,
            const ThreeDTile& tile,
            const hrz::blobs::BlobHandle& tile_data_blob,
            const ThreeDTile::SubtileIdentification& ident);

        static std::optional<ThreeDTile::Subtile> load_gltf_content(
            const ContentWorkContext& ctx,
            const Tileset& tileset,
            const ThreeDTile& tile,
            const hrz::blobs::BlobHandle& gltf_blob,
            const ThreeDTile::SubtileIdentification& ident);

        void update_appearance(const hrz::model::DrawProperties& inherited_draw_prps);
        void update_material_properties(const hrz::model::DrawProperties& inherited_draw_prps);

        void recreate_all_materials(std::span<const hrz_proto::Material> materials);
        void add_material(const hrz_proto::Material& material);
        void remove_material(size_t index);
        void update_material(size_t index, const hrz_proto::Material& material);
        void update_material_palette(size_t index, const hrz_proto::NumericPalette& palette);
        void update_active_materials(const TilesetConfig& tileset_config);

        void work(const ContentWorkContext& ctx);

        [[nodiscard]]
        ContentLoadStatusUpdate work_loading(
            const ContentWorkContext& ctx,
            const ThreeDTile& tile,
            const Subtile& subtile,
            const TilesetConfig* tileset_config);

        [[nodiscard]]
        hrz::RenderRequest work_gpu(
            const ContentWorkContext& ctx,
            const ContentRenderContext& render_ctx,
            const ThreeDTile& tile,
            const Subtile& subtile,
            const TilesetConfig* tileset_config);

        void draw(
            const ContentWorkContext& ctx,
            const ContentRenderContext& render_ctx,
            hrz::SceneViewBitset draw_in);

        void unload(const ContentWorkContext& ctx, std::vector<my::ResourceHandle>& to_destroy);

        void reset_style() {}

        void update_selection(
            const hrz::flat_hash_set<hrz::vector_data::FeatureIdHash>& selected_feature_ids);

        void update_feature_colors(
            std::span<const lm::ubvec4> per_batch_colors,
            bool has_transparent_color);
    };

    struct TilesetContent
    {
        TilesetH tileset_handle = 0;
    };

    class I3dmContent
    {
        // This is used for external glTF models to be able to retrieve the model prototype,
        // baked model, geometry, and material in the global cache.
        std::optional<size_t> _model_uri_hash;

        lm::dvec3 _quantized_volume_scale;
        bool _use_east_north_up_orientation = false;

        hrz::model::ModelPrototype* _prototype = nullptr;
        std::optional<hrz::model::InstancedBakedModelH> _model;
        std::optional<hrz::model::InstancedModelGeometryH> _geometry;
        std::optional<hrz::model::ModelMaterialH> _material;
        std::optional<hrz::model::InstanceGroupH> _instance_group = std::nullopt;

        hrz::model::DrawProperties _draw_prps;

        hrz::picking::ObjectReference _object_reference;
        hrz::picking::FeatureReference _feature_reference;

        std::vector<lm::vec3> _position_data;
        std::vector<lm::usvec3> _position_quantized_data;
        std::vector<lm::vec3> _normal_data;
        std::vector<lm::usvec4> _normal_oct32p_data;
        std::vector<lm::vec3> _scale_data;
        std::vector<uint32_t> _batch_id_data;

        std::vector<lm::ubvec4> _per_instance_color = std::vector<lm::ubvec4>(0);
        uint32_t _instances_length = 0;

        static bool decode_i3dm_feature_table(
            Subtile* subtile,
            std::span<const std::byte> feature_table_json_data,
            const hrz::blobs::BlobHandle& feature_table_bin_blob);

        void create_instance_group(
            const ThreeDTile& tile,
            const Subtile& subtile,
            const TilesetConfig* tileset_config);

    public:
        static std::optional<ThreeDTile::Subtile> load_content(
            const ContentWorkContext& ctx,
            const Tileset& tileset,
            const ThreeDTile& tile,
            const hrz::blobs::BlobHandle& tile_data_blob,
            const ThreeDTile::SubtileIdentification& ident,
            ModelCache& model_cache);

        void update_appearance(const hrz::model::DrawProperties& inherited_draw_prps);
        void update_material_properties(const hrz::model::DrawProperties& inherited_draw_prps);

        void work(const ContentWorkContext& ctx);

        [[nodiscard]]
        ContentLoadStatusUpdate work_loading(
            const ContentWorkContext& ctx,
            const ThreeDTile& tile,
            const Subtile& subtile,
            const TilesetConfig* tileset_config);

        [[nodiscard]]
        hrz::RenderRequest work_gpu(
            const ContentWorkContext& ctx,
            const ContentRenderContext& render_ctx,
            const ThreeDTile& tile,
            const Subtile& subtile,
            const TilesetConfig* tileset_config);

        void draw(
            const ContentWorkContext& ctx,
            const ContentRenderContext& render_ctx,
            hrz::SceneViewBitset draw_in);

        void unload(const ContentWorkContext& ctx, std::vector<my::ResourceHandle>& to_destroy);

        void reset_style();

        void update_selection(
            const hrz::flat_hash_set<hrz::vector_data::FeatureIdHash>& selected_feature_ids);

        void update_feature_colors(
            std::span<const lm::ubvec4> per_batch_colors,
            bool has_transparent_color);
    };

    class PntsContent
    {
        hrz::Observed<hrz::PointCloud::UniformData> _uniform_data;

        // Once the point_cloud is built, this should be destroyed to release the blobs.
        // Unless point_count == 0, in which case point_cloud is not built and geometry stays alive
        // so that we can check that the tile is empty (and thus valid).
        // This is OK because there won't be any blobs loaded.
        std::optional<hrz::PointCloud::Geometry> _geometry;
        std::unique_ptr<hrz::PointCloud> _point_cloud;

        bool geometry_has_positions() const;

        static bool decode_pnts_feature_table(
            Subtile* subtile,
            std::span<const std::byte> feature_table_json_data,
            const hrz::blobs::BlobHandle& feature_table_bin_blob,
            hrz::BlobAllocator* ba);

    public:
        static std::optional<ThreeDTile::Subtile> load_content(
            const ContentWorkContext& ctx,
            const TilesetConfig* tileset_config,
            const hrz::blobs::BlobHandle& tile_data_blob,
            const ThreeDTile::SubtileIdentification& ident);

        inline void update_appearance(const hrz::model::DrawProperties& draw_prps)
        {
            _uniform_data.mutate(
                [&draw_prps](hrz::PointCloud::UniformData& uniform)
                {
                    uniform.feature_color_blend_strength = draw_prps.feature_color_blend_strength;
                    uniform.feature_color_blend_mode = draw_prps.feature_color_blend_mode;
                    uniform.lighting_enabled = draw_prps.lighting.lighting_enabled;
                    uniform.receive_shadows = draw_prps.lighting.receive_shadows;
                    uniform.clip_id = draw_prps.clip_id;
                });
        }

        [[nodiscard]]
        ContentLoadStatusUpdate work_loading(
            const ContentWorkContext& ctx,
            const ThreeDTile& tile,
            const Subtile& subtile,
            const TilesetConfig* tileset_config);

        inline void update_material_properties(const hrz::model::DrawProperties& draw_prps)
        {
            update_appearance(draw_prps);
        }

        void work(const ContentWorkContext& /* ctx */) {}

        [[nodiscard]]
        hrz::RenderRequest work_gpu(
            const ContentWorkContext& ctx,
            const ContentRenderContext& render_ctx,
            const ThreeDTile& tile,
            const Subtile& subtile,
            const TilesetConfig* tileset_config);

        void draw(
            const ContentWorkContext& ctx,
            const ContentRenderContext& render_ctx,
            hrz::SceneViewBitset draw_in);

        void unload(const ContentWorkContext& ctx, std::vector<my::ResourceHandle>& to_destroy);

        void reset_style() {}

        void update_selection(
            const hrz::flat_hash_set<hrz::vector_data::FeatureIdHash>& selected_feature_ids);

        void update_feature_colors(
            std::span<const lm::ubvec4> per_batch_colors,
            bool has_transparent_color);
    };

    struct Subtile
    {
        using ContentVariant =
            std::variant<std::monostate, B3dmContent, TilesetContent, I3dmContent, PntsContent>;

        enum class LoadStatus
        {
            LOADING,
            LOADED,
            ERROR,
        };

        ContentVariant content;

        lm::dmat4 rtc_transform = lm::dmat4::identity(); // From the RTC field in the feature table.

        // Each subtile may have many batches. In order to identify a batch inside a tile, each
        // subtile has a distinct range of batch ids :
        // [batch_id_offset; batch_id_offset + batch_length[.
        uint32_t batch_id_offset = 0;
        uint32_t batch_length = 0;

        hrz::vector_data::FeatureIds batches_to_feature_ids;

        // The batch table (attribute_values/batches_to_feature_ids above) is decoded the same
        // way for every content type, so the job is polled once here rather than once per type.
        hrz_jobs::DecodeThreeDTilesBatchTableTicket decode_batch_table_ticket;
        bool batch_table_decoded = false;

        std::vector<lm::ubvec4> per_batch_color{};
        bool has_transparent_batch = false;
        bool has_been_styled_once = false;
        bool needs_restyling = false;
        bool needs_attribute_reload = false; // Only for attributes from the vector data layer.

        enum class BatchTablePollResult
        {
            PENDING,
            DECODED, // Set on the frame the job completes, and on every frame after that.
            ERROR,
        };

        // Polls `decode_batch_table_ticket` and, the first time it finishes successfully,
        // moves its results into `attribute_values` and `batches_to_feature_ids`.
        BatchTablePollResult poll_batch_table(hrz::JobScheduler* js);

        // Returns whether all the attributes sourced from the vector data layer were found.
        bool store_vector_data_attributes(
            std::span<const hrz::three_d_tiles::AttributeConfig> attributes,
            std::span<hrz::vector_data::AttributeValues> values,
            hrz::AttributionHandle attribution);

        void clear_vector_data_attributes(
            std::span<const hrz::three_d_tiles::AttributeConfig> attributes);

        enum class VectorDataAttributeStatus
        {
            UNREQUESTED,
            LOADING,
            LOADED,
            ERROR,
        };

        uint64_t vector_data_attribute_request_id = 0;
        VectorDataAttributeStatus vector_data_attribute_status =
            VectorDataAttributeStatus::UNREQUESTED;
        std::vector<std::optional<hrz::vector_data::AttributeValues>> attribute_values;
        hrz::AttributionHandle attribute_attributions;

        constexpr LoadStatus load_status() const { return _load_status; }

        constexpr StylingStatus styling_status() const { return _styling_status; }

        void set_styling_status(StylingStatus status);

        // Abandons any styling in progress, from any state.
        void reset_styling_status(hrz::JobScheduler* js);

        hrz_jobs::StyleFeaturesTicket style_job_ticket;

        constexpr bool has_external_tileset() const
        {
            return std::holds_alternative<TilesetContent>(content);
        }

        constexpr bool has_b3dm() const { return std::holds_alternative<B3dmContent>(content); }

        constexpr bool has_i3dm() const { return std::holds_alternative<I3dmContent>(content); }

        constexpr bool has_pnts() const { return std::holds_alternative<PntsContent>(content); }

        constexpr bool can_be_styled() const { return has_b3dm() || has_i3dm() || has_pnts(); }

        constexpr bool is_styled() const { return !can_be_styled() || has_been_styled_once; }

        hrz::RenderRequest reset_style();

        void work(const ContentWorkContext& ctx);

        [[nodiscard]]
        hrz::RenderRequest work_gpu(
            const ContentWorkContext& ctx,
            const ContentRenderContext& render_ctx,
            const ThreeDTile& tile,
            const TilesetConfig* tileset_config);

        hrz::RenderRequest upload_style_data();

    private:
        friend struct ThreeDTile;

        LoadStatus _load_status = LoadStatus::LOADING;
        StylingStatus _styling_status = StylingStatus::IDLE;
    };

    void add_loaded_subtile(
        const Tileset& tileset,
        Subtile&& subtile,
        SubtileIdentification* subtile_ident);

    // All changes to a subtile's load status go through here, to keep
    // `subtiles_left_to_load` in sync.
    void set_subtile_load_status(Subtile& subtile, Subtile::LoadStatus status);

    inline void clear_subtiles()
    {
        subtiles.clear();
        _subtiles_left_to_load = 0;
    }

    // What this tile contributes to its parent's `active_child_count`.
    constexpr bool counts_as_active_child() const { return _load_status != LoadStatus::UNLOADED; }

    // What this tile contributes to its parent's `renderable_child_count`. A tile that was
    // renderable keeps counting while its content reloads, and a tile with a viewer volume
    // counts as soon as it's loaded, since it may never load its content.
    constexpr bool counts_as_renderable_child() const
    {
        return _load_status != LoadStatus::UNLOADED
            && (viewer_bounding_volume.has_value() || _was_made_renderable_once);
    }

#if HRZ_DEBUG
    void check_invariants() const;
#endif

    ThreeDTile() = default;

    HRZ_DELETE_COPY(ThreeDTile);
    HRZ_DEFAULT_MOVE(ThreeDTile);

    constexpr LoadStatus load_status() const { return _load_status; }

    hrz::SceneViewBitset render_self_in;
    hrz::SceneViewBitset render_children_in;
    hrz::SceneViewBitset culled_in;

    constexpr hrz::SceneViewBitset render_none_in() const
    {
        return (!render_self_in) & (!render_children_in);
    }

    constexpr hrz::SceneViewBitset render_only_self_in() const
    {
        return render_self_in & (!render_children_in);
    }

    inline void set_render_self_in(hrz::SceneViewBitset bitset)
    {
        render_self_in |= bitset;
        render_children_in &= !bitset;
    }

    inline void set_render_children_in(hrz::SceneViewBitset bitset)
    {
        render_self_in &= !bitset;
        render_children_in |= bitset;
    }

    inline void set_render_self_and_children_in(hrz::SceneViewBitset bitset)
    {
        render_self_in |= bitset;
        render_children_in |= bitset;
    }

    inline void set_render_none_in(hrz::SceneViewBitset bitset)
    {
        render_self_in &= !bitset;
        render_children_in &= !bitset;
    }

    hrz::SceneViewBitset draw_in(const TilesetConfig& config) const;

    static ThreeDTile from_descriptor(
        hrz::three_d_tiles::ThreeDTilesTilesetDescriptor::Tile&& descriptor_tile,
        uint32_t index);

    inline void forget_inactive_views(hrz::SceneViewBitset visiting_in)
    {
        should_render_children_in &= visiting_in;
        render_children_in &= visiting_in;
        render_self_in &= visiting_in;

        for (size_t i = 0; i < hrz::SCENE_VIEW_COUNT; ++i)
        {
            if (!visiting_in.is_set(i))
            {
                rendered_child_count_per_view[i] = 0;
            }
        }
    }

    uint32_t depth;
    uint32_t index;
    uint32_t parent_index;
    int child_count;
    unsigned int first_child_link_index;
    hrz::three_d_tiles::RefinementType refinement_type;

    lm::dmat4 base_transform; // From the tileset descriptor.
    lm::mat3 normal_transform;
    BoundingVolume bounding_volume;
    std::optional<BoundingVolume> viewer_bounding_volume;
    std::optional<BoundingVolume> content_bounding_volume;
    std::optional<lm::dvec3> horizon_occlusion_point;

    // Set to true of the tile is empty, and has a larger geometric error than
    // its parents. This means that this tile doesn't go through the usual refinement logic.
    // Children are loaded as soon as this tile is needed.
    bool is_empty_structural;

    double geometric_error;

    std::string uri;
    std::optional<hrz::three_d_tiles::ThreeDTilesTilesetDescriptor::Range> range;
    hrz::assets_loader::Ticket load_data_ticket;

    constexpr int active_child_count() const { return _active_child_count; }

    constexpr int renderable_child_count() const { return _renderable_child_count; }

    int rendered_child_count_per_view[hrz::SCENE_VIEW_COUNT]; // Actually rendered, or using a
                                                              // viewer request volume.

    hrz::SceneViewBitset
        should_render_children_in; // If they are renderable and this tile should be rendered too.

    bool delete_if_unused;

    hrz::InlinedVector<Subtile, 1> subtiles;

    bool has_unsupported_subtiles = false;

    constexpr size_t subtiles_left_to_load() const { return _subtiles_left_to_load; }

private:
    friend struct Tileset;

    // Only changed through `Tileset::set_tile_load_status`, except at creation.
    LoadStatus _load_status;
    bool _was_made_renderable_once = false;

    // Children for which `counts_as_active_child` and `counts_as_renderable_child` are true.
    int _active_child_count;
    int _renderable_child_count;

    size_t _subtiles_left_to_load = 0;
};

struct TilesetConfig
{
    struct ScreenSpaceErrorHysteresis
    {
        double min_error;
        double max_error;

        static inline ScreenSpaceErrorHysteresis from_refinement_hysteresis(
            double refinement_hysteresis)
        {
            refinement_hysteresis = std::max(0.0, refinement_hysteresis);
            return {std::exp(-refinement_hysteresis), std::exp(refinement_hysteresis)};
        }
    };

    enum class VectorDataLayerStatus
    {
        UNREQUESTED,
        LOADING,
        LOADED,
        ERROR,
    };

    // All of this is a bit redundant because there is a one-to-one mapping between
    // each of them, but they each have a purpose, and it's not like a few integers
    // of waste will kill us.
    LayerH layer_handle;      // Internal handle in the pool in the system.
    uint64_t global_layer_id; // Public ID of the layer.

    bool is_visible;
    hrz::layers::MultiviewVisibilityConstraints visibility_constraints_result;
    hrz::AttributionHandle additional_attribution;

    double max_screen_space_error;
    ScreenSpaceErrorHysteresis screen_space_error_hysteresis;
    std::vector<hrz::three_d_tiles::AttributeConfig> attributes;
    bool has_vector_data_layer_attributes;
    std::vector<hrz::Palette> style_palettes;

    uint32_t vector_data_layer_id;
    uint64_t vector_data_layer_request_id;
    VectorDataLayerStatus vector_data_layer_status;
    bool renew_vector_data_layer_request;

    int8_t loading_priority;
    hrz_proto::LayerVisibilityConstraintList visibility_constraints;
    bool draw_under_flat_overlays;
    SceneViewBitset scene_views_bitset;
    hrz::HttpHeaders headers;

    hrz::model::DrawProperties inherited_draw_prps;

    std::vector<hrz_proto::Material> materials;
    std::string base_material;
    std::string overlay_material;

    std::shared_ptr<hrz::style::FlatAst> style_ast;
    bool has_style;
    uint64_t rng_seed;

    hrz::flat_hash_set<hrz::vector_data::FeatureIdHash> selected_feature_ids;

    inline hrz::assets_loader::Queue get_request_queue() const
    {
        return hrz::get_request_queue(loading_priority, hrz::assets_loader::Queue::ThreeDTiles);
    }

    hrz::RenderRequest update_visibility_constraints(
        std::span<const hrz::RenderViewInfo> views_info);
};

struct Tileset
{
    enum class Status
    {
        LOADING_DESCRIPTOR,
        DECODING_DESCRIPTOR,
        LOADED,
        ERROR
    };

    const TilesetConfig* config; // Shared with the external tilesets loaded from this tileset.
    std::unique_ptr<TilesetConfig> owned_config; // Only set on root tilesets.
    Status status;
    hrz::assets_loader::Ticket load_descriptor_ticket;
    hrz_jobs::DecodeThreeDTilesTilesetTicket decode_descriptor_ticket;
    std::string url;
    hrz::BaseUrl base_url = {{}, false};

    // Only half of the full request IDs. Identifiers for the tile and subtile
    // must be added in order to form a complete request ID.
    uint32_t vector_data_attribute_request_id;

    lm::dmat4 transform;
    double geometric_error;
    uint32_t root_depth = 0;

    std::vector<ThreeDTile> tiles = std::vector<ThreeDTile>(0);
    unsigned int root_tile_index;
    std::vector<unsigned int> child_links;
    bool allow_gltf_content;
    hrz::three_d_tiles::GltfUpAxis gltf_up_axis;

    hrz::flat_hash_set<uint32_t> active_tiles; // Loading, loaded, or renderable

    bool render; // Used when the tileset is external to another one.

    hrz::metrics::MetricDesc requests_count_metric;

    inline bool is_root() const { return owned_config != nullptr; }

    inline TilesetConfig* get_mutable_config()
    {
        assert(is_root());
        return owned_config.get();
    }

    // Returns false if the descriptor has no tiles, in which case the tileset is in error.
    bool apply_descriptor(hrz::three_d_tiles::ThreeDTilesTilesetDescriptor&& descriptor);

    // This function is used to determine whether or not the root tile of an external
    // tileset can replace the parent tile (of content type `EXTERNAL_TILESET`.)
    inline ThreeDTile::LoadStatus get_root_tile_status() const
    {
        if (status == Tileset::Status::ERROR) return ThreeDTile::LoadStatus::ERROR;
        if (status != Tileset::Status::LOADED) return ThreeDTile::LoadStatus::UNLOADED;

        // If the root tile does not exist, their is no tile that
        // can be used to replace the parent tile (of the parent
        // tileset), so it's an error.
        if (tiles.empty()) return ThreeDTile::LoadStatus::ERROR;

        return tiles.at(root_tile_index).load_status();
    }

    // Returns the previous lower (or equal) geometric error in the parent chain,
    // or return the tile's own geometric error if no lower value has been found.
    double get_lower_geometric_error_in_parent_tiles(uint32_t tile_index) const;

    hrz::RenderRequest make_tile_renderable(uint32_t tile_index);

    // All changes to a tile's load status go through here, to keep its parent's child
    // counters in sync.
    void set_tile_load_status(ThreeDTile& tile, ThreeDTile::LoadStatus status);

#if HRZ_DEBUG
    void check_tile_invariants(const ThreeDTile& tile) const;
#endif

    void start_decoding_descriptor(
        const hrz::blobs::BlobHandle& descriptor_blob,
        hrz::JobScheduler* js);
};

inline hrz::SceneViewBitset ThreeDTile::draw_in(const TilesetConfig& config) const
{
    return render_self_in & !culled_in
        & hrz::SceneViewBitset(config.visibility_constraints_result.satisfied_in)
        & config.scene_views_bitset;
}

template<typename T>
concept ContentSubtile = requires(
    T& content,
    const ContentWorkContext& ctx,
    const ContentRenderContext& render_ctx,
    const ThreeDTile& tile,
    const ThreeDTile::Subtile& subtile,
    const TilesetConfig* tileset_config,
    std::vector<my::ResourceHandle>& to_destroy,
    hrz::SceneViewBitset draw_in,
    const hrz::model::DrawProperties& draw_prps,
    const hrz::flat_hash_set<hrz::vector_data::FeatureIdHash>& selected_feature_ids,
    std::span<const lm::ubvec4> per_batch_colors,
    bool has_transparent_color) {
    { content.work(ctx) } -> std::same_as<void>;
    {
        content.work_loading(ctx, tile, subtile, tileset_config)
    } -> std::same_as<ThreeDTile::ContentLoadStatusUpdate>;
    {
        content.work_gpu(ctx, render_ctx, tile, subtile, tileset_config)
    } -> std::same_as<hrz::RenderRequest>;
    { content.draw(ctx, render_ctx, draw_in) } -> std::same_as<void>;
    { content.unload(ctx, to_destroy) } -> std::same_as<void>;
    { content.update_appearance(draw_prps) } -> std::same_as<void>;
    { content.update_material_properties(draw_prps) } -> std::same_as<void>;
    { content.update_selection(selected_feature_ids) } -> std::same_as<void>;
    {
        content.update_feature_colors(per_batch_colors, has_transparent_color)
    } -> std::same_as<void>;
    { content.reset_style() } -> std::same_as<void>;
};

static_assert(ContentSubtile<ThreeDTile::B3dmContent>);
static_assert(ContentSubtile<ThreeDTile::I3dmContent>);
static_assert(ContentSubtile<ThreeDTile::PntsContent>);

} // namespace hrz::three_d_tiles
