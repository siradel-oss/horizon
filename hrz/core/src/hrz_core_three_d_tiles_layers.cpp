#include "hrz_core_three_d_tiles_layers.h"

#include "assets_loader/hrz_core_assets_loader.h"
#include "hrz_core_base_url.h"
#include "hrz_core_debug_draw.h"
#include "hrz_core_global_flags.h"
#include "hrz_core_job_scheduler.h"
#include "hrz_core_loading_priorities.h"
#include "hrz_core_picking_id_allocator.h"
#include "hrz_core_point_cloud.h"
#include "hrz_core_render.h"
#include "hrz_core_scene.h"
#include "hrz_core_scene_model_array_sync.h"
#include "hrz_core_selection.h"
#include "hrz_core_shadows.h"
#include "hrz_core_sky.h"
#include "hrz_core_style_script.h"
#include "hrz_core_viewsheds.h"
#include "hrz_core_visibility_constraints.h"
#include "model/hrz_core_model.h"
#include "model/hrz_core_model_descriptor.h"
#include "model/hrz_core_model_materials_manager.h"
#include "vector/data_loader/hrz_core_vector_data_loader.h"

#include <hrz_common_attributes.h>
#include <hrz_common_blob_allocator.h>
#include <hrz_common_color.h>
#include <hrz_common_horizon_culling.h>
#include <hrz_common_maths.h>
#include <hrz_common_metrics.h>
#include <hrz_common_monitoring_defs.h>
#include <hrz_common_palette.h>
#include <hrz_common_profiling.h>
#include <hrz_common_proto_geo.h>
#include <hrz_common_proto_maths.h>
#include <hrz_common_style.h>
#include <hrz_common_three_d_tiles.h>
#include <hrz_common_vertex_utils.h>
#include <hrz_core_shaders.h>
#include <hrz_fnd_array_view.h>
#include <hrz_fnd_flat_hash_map.h>
#include <hrz_fnd_flat_hash_set.h>
#include <hrz_fnd_gen_object_pool.h>
#include <hrz_fnd_hash.h>
#include <hrz_fnd_json_utils.h>
#include <hrz_fnd_log.h>
#include <hrz_fnd_maths.h>
#include <hrz_fnd_mem.h>
#include <hrz_fnd_meta.h>
#include <hrz_fnd_observed.h>
#include <hrz_fnd_static_string.h>
#include <hrz_fnd_string_utils.h>
#include <hrz_fnd_time.h>
#include <hrz_fnd_url_utils.h>
#include <hrz_fnd_variant.h>
#include <hrz_jobs_tickets.h>
#include <hrz_protocol_path_builder.h>

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

// Let's talk about picking a bit.
//
// Object references use the following format:
// tttttttiiiiissss iiiiiiibbbbbbbbb
// ---------------- ----------------
// rrrrrrrrrrrrrrrr gggggggggggggggg
// layer_id         object_id
//
// Each character represents two bits. 64 bits in total.
// Least significant bits are on the right.
// s: System picking id
// t: Tileset handle
// i: Tile index
// b: batch id
// The batch id is added to the low picking id value in
// the fragment shader.
// So in the end we have:
//  * 8 bits, or 256 values, for the system id (we don't control that),
//  * 14 bits, or 16,384 values, for the tileset handle,
//  * 24 bits, or 16,777,216 values, for the tile index,
//  * 18 bits, or 262,144 values, for the batch id.
//
// In the model system and shaders, batch ids are simply added to the
// object_id part, so those bits need to be the lowest bits.
//
// Feature references simply use the local layer handle in the layer_id part.

#define DRAW_DEBUG_BOXES 0

#if DRAW_DEBUG_BOXES
#    include "hrz_core_shaders.h"
#endif

namespace
{
using hrz::three_d_tiles::BoundingVolume;
using LayerH = uint32_t;
using TilesetH = uint32_t;

enum
{
    Box_UboTileParams = hrz::UboCustomStart,

    Box_PositionVertexInput = 0,
    Box_NormalVertexInput = 1,
    Box_ColorVertexInput = 2,
};

static constexpr double REQUEST_PRIORITY_UPDATE_INTERVAL = 500; // ms

static constexpr uint64_t COLOR_PRP = 0;
static constexpr uint32_t TILE_REPR_ID = 0;
static constexpr const char* TILE_REPR_NAME = "tile";

#if DRAW_DEBUG_BOXES
struct BoxUniformData
{
    lm::vec4 center_low;
    lm::vec4 center_high;
    lm::mat4 transform;
    hrz::GlslStd140Mat3 normal_transform;
};

struct RenderableBox : public my::Renderer::Renderable
{
    my::ResourceHandle pos_normal_buffer = my::ResourceHandle::null();
    my::ResourceHandle color_buffer = my::ResourceHandle::null();

    struct RenderData
    {
        my::ResourceHandle vertex_input = my::ResourceHandle::null();
        uint16_t vertex_count = 0;
        my::ResourceHandle shader = my::ResourceHandle::null();
        my::ResourceHandle ubo_buffer = my::ResourceHandle::null();
    } data;

    my::Renderer::BinMask bin_mask;

    lm::dvec3 center;
    double radius;

    static void render_callback(
        uint32_t render_type,
        my::RenderContext* r,
        my::ResourceBinder* rb,
        const void* user_data_raw,
        const void* raw_data)
    {
        auto data = (const RenderData*)raw_data;

        my::ResourceHandle shader;
        switch (render_type)
        {
            case hrz::RenderVisual: shader = data->shader; break;
            default: return;
        }

        auto batch = my::DrawBatchInfo(my::PrimitiveType::TriangleList, data->vertex_count);

        rb->push_state();

        my::UboBinding ubo_bindings[] = {
            {Box_UboTileParams, data->ubo_buffer, 0, sizeof(BoxUniformData)}};
        rb->bind(HRZ_ARRAY_COUNT(ubo_bindings), ubo_bindings);

        auto state = rb->get_current_state();

        r->draw(
            batch, shader, data->vertex_input, state.ubo_count, state.ubos, state.texture_count,
            state.textures);

        rb->pop_state();
    }

    void collect_render_info(my::Renderer::Queue& queue, const my::Renderer::Culler& culler)
        const override
    {
        if (culler.is_visible_in_any_view(center, radius))
        {
            queue.enqueue(bin_mask, render_callback, &data, center, radius);
        }
    }
};

std::vector<lm::vec3> make_box(const BoundingVolume& volume)
{
    std::vector<lm::vec3> vbo;

    auto make_pos = [&](const lm::dvec3& v) { return lm::vec3(v - volume.center); };

    auto make_face =
        [&](const lm::dvec3& v0, const lm::dvec3& v1, const lm::dvec3& v2, const lm::dvec3& v3)
    {
        auto p0 = make_pos(v0);
        auto p1 = make_pos(v1);
        auto p2 = make_pos(v2);
        auto p3 = make_pos(v3);
        auto normal = lm::normalize(lm::cross(p1 - p0, p2 - p0));
        vbo.push_back(p0);
        vbo.push_back(normal);
        vbo.push_back(p1);
        vbo.push_back(normal);
        vbo.push_back(p2);
        vbo.push_back(normal);
        vbo.push_back(p0);
        vbo.push_back(normal);
        vbo.push_back(p2);
        vbo.push_back(normal);
        vbo.push_back(p3);
        vbo.push_back(normal);
    };

    auto make_box_mesh = [&](const lm::dvec3& ecef0, const lm::dvec3& ecef1, const lm::dvec3& ecef2,
                             const lm::dvec3& ecef3, const lm::dvec3& ecef4, const lm::dvec3& ecef5,
                             const lm::dvec3& ecef6, const lm::dvec3& ecef7)
    {
        make_face(ecef0, ecef1, ecef3, ecef2);
        make_face(ecef1, ecef5, ecef7, ecef3);
        make_face(ecef3, ecef7, ecef6, ecef2);
        make_face(ecef2, ecef6, ecef4, ecef0);
        make_face(ecef0, ecef4, ecef5, ecef1);
        make_face(ecef5, ecef4, ecef6, ecef7);
    };

    auto make_mesh_from_box = [&](const BoundingVolume::Box& box)
    {
        auto half_u_axis = box.u_axis * box.u_half_length;
        auto half_v_axis = box.v_axis * box.v_half_length;
        auto half_w_axis = box.w_axis * box.w_half_length;

        make_box_mesh(
            box.center - half_u_axis - half_v_axis - half_w_axis,
            box.center - half_u_axis - half_v_axis + half_w_axis,
            box.center - half_u_axis + half_v_axis - half_w_axis,
            box.center - half_u_axis + half_v_axis + half_w_axis,
            box.center + half_u_axis - half_v_axis - half_w_axis,
            box.center + half_u_axis - half_v_axis + half_w_axis,
            box.center + half_u_axis + half_v_axis - half_w_axis,
            box.center + half_u_axis + half_v_axis + half_w_axis);
    };

    auto make_mesh_from_sphere = [&](const BoundingVolume::Sphere& sphere)
    {
        lm::dvec3 half_x = {sphere.radius, 0, 0};
        lm::dvec3 half_y = {0, sphere.radius, 0};
        lm::dvec3 half_z = {0, 0, sphere.radius};

        make_box_mesh(
            sphere.center - half_x - half_y - half_z, sphere.center - half_x - half_y + half_z,
            sphere.center - half_x + half_y - half_z, sphere.center - half_x + half_y + half_z,
            sphere.center + half_x - half_y - half_z, sphere.center + half_x - half_y + half_z,
            sphere.center + half_x + half_y - half_z, sphere.center + half_x + half_y + half_z);
    };

    if (std::holds_alternative<BoundingVolume::Region>(volume.volume))
    {
        const auto& region = std::get<BoundingVolume::Region>(volume.volume);

        if (region.box.has_value())
        {
            make_mesh_from_box(region.box.value());
        }
        else if (region.sphere.has_value())
        {
            make_mesh_from_sphere(region.sphere.value());
        }
    }
    else if (std::holds_alternative<BoundingVolume::Box>(volume.volume))
    {
        make_mesh_from_box(std::get<BoundingVolume::Box>(volume.volume));
    }
    else if (std::holds_alternative<BoundingVolume::Sphere>(volume.volume))
    {
        make_mesh_from_sphere(std::get<BoundingVolume::Sphere>(volume.volume));
    }
    else
    {
        assert(false && "Unhandled case");
    }

    return vbo;
}
#endif

inline uint32_t read_uint32(std::span<const std::byte> span, const size_t offset)
{
    static_assert(
        std::endian::native == std::endian::little, "b3dm and i3dm file formats are little-endian");

    uint32_t v;
    std::memcpy(&v, (const uint32_t*)(span.data() + offset * sizeof(uint32_t)), sizeof(uint32_t));
    return v;
}

// Y and Z axes must be switched, see
// https://github.com/CesiumGS/3d-tiles/tree/master/specification#y-up-to-z-up
static const lm::dmat4 s_axes_transform = lm::dmat4(
    lm::dvec4(1, 0, 0, 0),
    lm::dvec4(0, 0, 1, 0),
    lm::dvec4(0, -1, 0, 0),
    lm::dvec4(0, 0, 0, 1));

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
        WAITING_FOR_VECTOR_DATA_LAYER,
        WAITING_FOR_ATTRIBUTE_VALUES,
        WAITING_FOR_JOB,
        WAITING_FOR_RESOURCE_UPDATE,
        IDLE
    };

    ThreeDTile() = default;

    ThreeDTile(ThreeDTile&&) = default;
    ThreeDTile& operator=(ThreeDTile&&) = default;

    LoadStatus load_status;
    bool was_made_renderable_once = false;
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

    int active_child_count;     // Loading, loaded, or renderable
    int renderable_child_count; // Actually renderable, or using a viewer request volume.
    int rendered_child_count_per_view[hrz::SCENE_VIEW_COUNT]; // Actually rendered, or using a
                                                              // viewer request volume.

    hrz::SceneViewBitset
        should_render_children_in; // If they are renderable and this tile should be rendered too.

    bool delete_if_unused;

    struct B3dmContent
    {
        hrz::model::ModelPrototype* prototype = nullptr;
        std::optional<hrz::model::BatchedModelGeometryH> geometry;
        hrz::model::MaterialsManager<hrz::model::BatchedModelMaterialManagerTraits> materials;

        hrz::model::DrawProperties draw_prps;

        hrz_jobs::DecodeThreeDTilesBatchTableTicket decode_batch_table_ticket;
        bool has_been_styled_once = false;
        std::vector<lm::ubvec4> per_batch_color = std::vector<lm::ubvec4>(0);

        uint32_t batch_length = 0;
    };

    struct TilesetContent
    {
        TilesetH tileset_handle = 0;
    };

    struct I3dmContent
    {
        // This is used for external glTF models to be able to retrieve the model prototype,
        // baked model, geometry, and material in the global cache.
        std::optional<size_t> model_uri_hash;

        lm::dvec3 quantized_volume_offset;
        lm::dvec3 quantized_volume_scale;
        bool use_east_north_up_orientation = false;

        hrz::model::ModelPrototype* prototype = nullptr;
        std::optional<hrz::model::InstancedBakedModelH> model;
        std::optional<hrz::model::InstancedModelGeometryH> geometry;
        std::optional<hrz::model::ModelMaterialH> material;
        std::optional<hrz::model::InstanceGroupH> instance_group = std::nullopt;
        hrz_jobs::DecodeThreeDTilesBatchTableTicket decode_batch_table_ticket;

        hrz::model::DrawProperties draw_prps;

        std::vector<lm::vec3> position_data;
        std::vector<lm::usvec3> position_quantized_data;
        std::vector<lm::vec3> normal_data;
        std::vector<lm::usvec4> normal_oct32p_data;
        std::vector<lm::vec3> scale_data;
        std::vector<uint32_t> batch_id_data;

        bool has_been_styled_once = false;
        std::vector<lm::ubvec4> per_instance_color = std::vector<lm::ubvec4>(0);

        uint32_t batch_length = 0;
        uint32_t instances_length = 0;
    };

    struct PntsContent
    {
        bool has_loaded_batch_table = false;
        hrz_jobs::DecodeThreeDTilesBatchTableTicket decode_batch_table_ticket;

        bool has_been_styled_once = false;
        bool has_transparent_feature = false;
        std::vector<lm::ubvec4> per_batch_color{};

        hrz::Observed<hrz::PointCloud::UniformData> uniform_data;

        // Once the point_cloud is built, this should be destroyed to release the blobs.
        // Unless point_count == 0, in which case point_cloud is not built and geometry stays alive
        // so that we can check that the tile is empty (and thus valid).
        // This is OK because there won't be any blobs loaded.
        std::optional<hrz::PointCloud::Geometry> geometry;
        std::unique_ptr<hrz::PointCloud> point_cloud;

        uint32_t batch_length = 0;

        void update_appearance(const hrz::model::DrawProperties& draw_prps)
        {
            uniform_data.mutate(
                [&draw_prps](hrz::PointCloud::UniformData& uniform)
                {
                    uniform.feature_color_blend_strength = draw_prps.feature_color_blend_strength;
                    uniform.feature_color_blend_mode = draw_prps.feature_color_blend_mode;
                    uniform.lighting_enabled = draw_prps.lighting.lighting_enabled;
                    uniform.receive_shadows = draw_prps.lighting.receive_shadows;
                    uniform.clip_id = draw_prps.clip_id;
                });
        }
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

        hrz::vector_data::FeatureIds batches_to_feature_ids;

        enum class VectorDataAttributeStatus
        {
            UNREQUESTED,
            LOADING,
            LOADED,
            ERROR,
        };

        uint64_t vector_data_attribute_request_id = 0;
        bool has_received_vector_data_attributes_once = false;
        VectorDataAttributeStatus vector_data_attribute_status =
            VectorDataAttributeStatus::UNREQUESTED;
        std::vector<std::optional<hrz::vector_data::AttributeValues>> attribute_values;
        hrz::AttributionHandle attribute_attributions;

        LoadStatus load_status = LoadStatus::LOADING;
        StylingStatus styling_status = ThreeDTile::StylingStatus::IDLE;
        hrz_jobs::StyleFeaturesTicket style_job_ticket;

        constexpr bool has_external_tileset() const
        {
            return std::holds_alternative<TilesetContent>(content);
        }

        constexpr bool has_b3dm() const { return std::holds_alternative<B3dmContent>(content); }

        constexpr bool has_i3dm() const { return std::holds_alternative<I3dmContent>(content); }

        constexpr bool has_pnts() const { return std::holds_alternative<PntsContent>(content); }

        uint32_t batch_length() const
        {
            return std::visit(
                hrz::overload{
                    [](const std::monostate&) { return 0U; },
                    [](const B3dmContent& c) { return c.batch_length; },
                    [](const TilesetContent&) { return 0U; },
                    [](const I3dmContent& c) { return c.batch_length; },
                    [](const PntsContent& c) { return c.batch_length; }},
                content);
        }
    };

    hrz::InlinedVector<Subtile, 1> subtiles;

    bool has_unsupported_subtiles = false;
    size_t subtiles_left_to_load = 0;

#if DRAW_DEBUG_BOXES
    struct BoxData
    {
        std::vector<lm::vec3> vertices;
        std::vector<lm::ubvec4> colors;
    } box;

    std::optional<RenderableBox> renderable_box;
#endif
};

struct TilesetConfig
{
    struct ScreenSpaceErrorHysteresis
    {
        double min_error;
        double max_error;
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

    enum class VectorDataLayerStatus
    {
        UNREQUESTED,
        LOADING,
        LOADED,
        ERROR,
    };

    uint32_t vector_data_layer_id;
    uint64_t vector_data_layer_request_id;
    VectorDataLayerStatus vector_data_layer_status;
    bool renew_vector_data_layer_request;

    int8_t loading_priority;
    hrz_proto::LayerVisibilityConstraintList visibility_constraints;
    bool draw_under_flat_overlays;
    uint32_t scene_views_bitset;
    hrz::HttpHeaders headers;

    hrz::model::DrawProperties inherited_draw_prps;

    std::vector<hrz_proto::Material> materials;
    std::string base_material;
    std::string overlay_material;

    std::shared_ptr<hrz::style::FlatAst> style_ast;
    bool has_style;
    uint64_t rng_seed;

    hrz::flat_hash_set<hrz::vector_data::FeatureIdHash> selected_feature_ids;
};

hrz::assets_loader::Queue get_request_queue(const TilesetConfig* config)
{
    return hrz::get_request_queue(config->loading_priority, hrz::assets_loader::Queue::ThreeDTiles);
}

struct Tileset
{
    enum class Status
    {
        LOADING_DESCRIPTOR,
        DECODING_DESCRIPTOR,
        LOADED,
        ERROR
    };

    bool is_external;
    TilesetConfig* config; // The config is owned only if the tileset is not external.
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

    hrz::flat_hash_set<uint32_t> active_tiles; // Loading, loaded, or renderable

    bool render; // Used when the tileset is external to another one.

    bool needs_attribute_reload; // Only for attributes from the vector data layer.
    bool needs_restyling;

    hrz::metrics::MetricDesc requests_count_metric;
};

// This function is used to determine whether or not the root tile of an external
// tileset can replace the parent tile (of content type `EXTERNAL_TILESET`.)
ThreeDTile::LoadStatus get_root_tile_status(const Tileset* tileset)
{
    if (tileset->status == Tileset::Status::ERROR) return ThreeDTile::LoadStatus::ERROR;
    if (tileset->status != Tileset::Status::LOADED) return ThreeDTile::LoadStatus::UNLOADED;

    // If the root tile does not exist, their is no tile that
    // can be used to replace the parent tile (of the parent
    // tileset), so it's an error.
    if (tileset->tiles.empty()) return ThreeDTile::LoadStatus::ERROR;

    return tileset->tiles.at(tileset->root_tile_index).load_status;
}

uint64_t make_model_uri_hash(std::string_view uri, const hrz::HttpHeaders& headers)
{
    return hrz::hash_mix(hrz::murmur3_x64_64(uri), headers.hash_content());
}

struct ThreeDTilesSystem
{
    using TilesetIndexPool = hrz::GenIndexPool<TilesetH, 1, 13>;
    using TilesetPool = hrz::GenObjectPool<Tileset, TilesetIndexPool, 64>;

    TilesetPool _tilesets;

    hrz::flat_hash_set<TilesetH> _loading_tilesets;
    hrz::flat_hash_set<TilesetH> _loaded_tilesets;
    hrz::flat_hash_set<TilesetH> _removed_tilesets;

    hrz::model::SharedResources* _model_shared_resources = nullptr;
    hrz::model::SharedResources* _model_shared_resources_instanced = nullptr;

    struct ModelPrototypeRef
    {
        enum class Status
        {
            NEW,
            LOADING,
            LOADED,
            ERROR
        };

        Status status = Status::ERROR;

        std::string uri;
        hrz::HttpHeaders headers;
        hrz::BaseUrl base_url;
        size_t data_offset;
        hrz::AttributionHandle additional_attribution;
        hrz::assets_loader::Queue load_queue;
        uint32_t loading_priority;
        hrz::monitoring::ResourceOwner resource_owner;

        hrz::assets_loader::Ticket load_external_gltf_ticket;
        hrz::model::ModelPrototype* prototype = nullptr;
        hrz::model::InstancedBakedModelH model;
        hrz::model::InstancedModelGeometryH geometry;
        hrz::model::ModelMaterialH material;

        uint32_t ref_count = 0;
    };

    // Cached model prototypes for i3dm external models.
    hrz::flat_hash_map<uint64_t, ModelPrototypeRef> _model_uris_to_prototypes;

    std::vector<my::ResourceHandle> _removed_resources;

    uint8_t _system_picking_id;

    double _request_priority_timer_ms;

    uint32_t _tile_depth_uniform_id = -1;

    hrz::vector_data::VectorDataLoaderChannel _vector_data_channel;

#if DRAW_DEBUG_BOXES
    my::ResourceHandle _box_shader = my::ResourceHandle::null();
#endif

    void init(
        uint8_t system_picking_id,
        hrz::vector_data::VectorDataLoaderChannel&& vector_data_channel)
    {
        _system_picking_id = system_picking_id;
        _vector_data_channel = std::move(vector_data_channel);
        _request_priority_timer_ms = hrz::now_frame_ms();
    }

    void initialize_rendering(hrz::Render* render)
    {
#if DRAW_DEBUG_BOXES
        {
            my::IndexName attribs[] = {
                {Box_PositionVertexInput, "i_vertex"},
                {Box_NormalVertexInput, "i_normal"},
                {Box_ColorVertexInput, "i_color"},
            };

            static const my::IndexName ubos[] = {
                {hrz::UboFrame, "Frame"},
                {Box_UboTileParams, "Tile"},
            };

            my::IndexName samplers[HRZ_S_MAX_SUN_CASCADES + 1] = {};
            for (int i = 0; i < HRZ_S_MAX_SUN_CASCADES; ++i)
            {
                samplers[i] = {
                    hrz::SamplerSunShadow0 + i, hrz::shadows::SUN_SHADOW_MAP_SAMPLER_NAMES[i]};
            }
            samplers[HRZ_S_MAX_SUN_CASCADES] = {
                hrz::SamplerSunColor, hrz::sky::SUN_COLOR_SAMPLER_NAME};

            static const char* outputs[] = {"o_color"};

            my::ShaderResource res{};
            res.name = hrz_shaders::Three_d_tiles_box_name;
            res.vertex_source_len = hrz_shaders::Three_d_tiles_box_vert_len;
            res.vertex_source = hrz_shaders::Three_d_tiles_box_vert;
            res.fragment_source_len = hrz_shaders::Three_d_tiles_box_frag_len;
            res.fragment_source = hrz_shaders::Three_d_tiles_box_frag;
            res.uniform_block_count = HRZ_ARRAY_COUNT(ubos);
            res.uniform_blocks = ubos;
            res.output_count = HRZ_ARRAY_COUNT(outputs);
            res.outputs = outputs;
            res.attribs = attribs;
            res.attrib_count = HRZ_ARRAY_COUNT(attribs);
            res.sampler_count = HRZ_ARRAY_COUNT(samplers);
            res.samplers = samplers;

            res.initial_state.depth.test = true;
            res.initial_state.depth.compare = my::DepthState::LessEqual;

            res.initial_state.color_blend.enable = false;
            res.initial_state.color_blend.color.src = my::ColorBlendState::SrcAlpha;
            res.initial_state.color_blend.color.dst = my::ColorBlendState::OneMinusSrcAlpha;

            res.initial_state.rasterization.cull_mode = my::RasterizationState::None;

            _box_shader = render->rc->alloc(&res, hrz::monitoring::systems::ThreeDTilesLayers);
        }
#endif

        if (!_model_shared_resources)
        {
            _model_shared_resources = hrz::model::create_shared_resources_batched(render);
        }

        if (!_model_shared_resources_instanced)
        {
            _model_shared_resources_instanced =
                hrz::model::create_shared_resources_instanced(render);
        }
    }

    TilesetH add_tileset(
        const std::string& url,
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
        hrz::AssetsLoader* al)
    {
        auto handle = _tilesets.alloc();
        auto tileset = _tilesets.get_object(handle);

        auto config = new TilesetConfig();

        config->is_visible = is_visible;
        config->visibility_constraints_result = hrz::layers::MultiviewVisibilityConstraints();
        config->visibility_constraints = visibility_constraints;
        config->draw_under_flat_overlays = draw_under_flat_overlays;
        config->additional_attribution = additional_attribution;

        config->layer_handle = layer_handle;
        config->global_layer_id = global_layer_id;

        // Avoid dividing by 0 later-on.
        config->max_screen_space_error = std::max(0.01, max_screen_space_error);
        config->screen_space_error_hysteresis =
            compute_screen_space_error_hysteresis(refinement_hysteresis);

        config->loading_priority = loading_priority;

        config->inherited_draw_prps.clip_id = clip_id;
        config->inherited_draw_prps.lighting = lighting_settings;
        config->inherited_draw_prps.color = lm::vec4(1.0F);
        config->inherited_draw_prps.apply_feature_color_to_overlay = false;
        config->inherited_draw_prps.overlay_material_enabled = false;
        config->inherited_draw_prps.overlay_material_opacity = 1.0F;
        config->inherited_draw_prps.transform = lm::dmat4::identity();
        config->inherited_draw_prps.feature_color_blend_mode = color_blend_mode;
        config->inherited_draw_prps.feature_color_blend_strength = color_blend_strength;

        config->attributes = attributes;
        config->has_vector_data_layer_attributes = std::ranges::any_of(
            config->attributes,
            [](const hrz::three_d_tiles::AttributeConfig& attribute)
            { return attribute.has_vector_data_layer_source(); });
        config->vector_data_layer_id = 0;
        config->vector_data_layer_request_id =
            ((uint64_t)handle << 32) + std::numeric_limits<uint32_t>::max();
        config->vector_data_layer_status = TilesetConfig::VectorDataLayerStatus::UNREQUESTED;
        config->renew_vector_data_layer_request = true;
        config->style_ast = std::make_shared<hrz::style::FlatAst>();
        config->has_style = false;

        config->scene_views_bitset = scene_views_bitset;

        tileset->config = config;
        tileset->is_external = false;

        tileset->transform = transform;

        // Nothing will be displayed if no value is given in the descriptor.
        tileset->geometric_error = 0;

        tileset->load_descriptor_ticket = hrz::assets_loader::begin(
            al, url.c_str(), headers, get_request_queue(tileset->config),
            hrz::combine_loading_priorities(
                tileset->config->loading_priority, std::numeric_limits<uint16_t>::max()),
            {hrz::monitoring::systems::ThreeDTilesLayers, global_layer_id});
        tileset->url = url;
        tileset->base_url = {url, preserve_query_parameters};
        tileset->config->headers = headers;
        tileset->vector_data_attribute_request_id = handle;
        tileset->status = Tileset::Status::LOADING_DESCRIPTOR;

        tileset->needs_attribute_reload = false;
        tileset->needs_restyling = false;

        tileset->requests_count_metric = hrz::metrics::MetricDesc(
            "3D Tiles (requests tally)", false,
            {{"layer", std::to_string(layer_handle)}, {"url", tileset->base_url.base().c_str()}});
        hrz::metrics::increment_counter(&tileset->requests_count_metric);

        _loading_tilesets.insert(handle);

        return handle;
    }

    const Tileset* _get_tileset(TilesetH handle) const
    {
        auto tileset = _tilesets.get_object(handle);

        if (tileset == nullptr)
        {
            HRZ_LOG_WARNING("Unknown tileset handle: {}", handle);
            return nullptr;
        }

        if (tileset->is_external)
        {
            HRZ_LOG_WARNING("Cannot configure external tileset ({}) directly", handle);
            return nullptr;
        }

        return tileset;
    }

    Tileset* _get_tileset(TilesetH handle, bool allow_external_tilesets = false)
    {
        auto tileset = _tilesets.get_object(handle);

        if (tileset == nullptr)
        {
            HRZ_LOG_WARNING("Unknown tileset handle: {}", handle);
            return nullptr;
        }

        if (tileset->is_external && !allow_external_tilesets)
        {
            HRZ_LOG_WARNING("Cannot configure external tileset ({}) directly", handle);
            return nullptr;
        }

        return tileset;
    }

    void _trigger_restyling(Tileset* tileset, bool reload_attributes)
    {
        tileset->needs_attribute_reload |= reload_attributes;
        tileset->needs_restyling = true;

        for (auto& tile : tileset->tiles)
        {
            for (auto& subtile : tile.subtiles)
            {
                if (subtile.has_external_tileset())
                {
                    auto& content = std::get<ThreeDTile::TilesetContent>(subtile.content);
                    if (content.tileset_handle != 0)
                    {
                        auto external_tileset = _tilesets.get_object(content.tileset_handle);
                        _trigger_restyling(external_tileset, reload_attributes);
                    }
                }
            }
        }
    }

    void set_vector_data_layer_id(TilesetH handle, uint32_t vector_data_layer_id)
    {
        auto tileset = _get_tileset(handle);
        if (tileset == nullptr) return;

        auto config = tileset->config;

        config->vector_data_layer_id = vector_data_layer_id;

        if (tileset->status == Tileset::Status::LOADED)
        {
            config->renew_vector_data_layer_request = true;
        }

        _trigger_restyling(tileset, true);
    }

    void set_style(
        TilesetH handle,
        const std::string& style_script,
        std::span<const hrz_proto::Palette* const> palettes_proto,
        uint64_t rng_seed)
    {
        auto tileset = _get_tileset(handle);
        if (tileset == nullptr) return;

        auto config = tileset->config;

        config->style_ast = std::make_shared<hrz::style::FlatAst>();

        config->style_palettes.clear();
        config->style_palettes.reserve(palettes_proto.size());
        for (auto proto : palettes_proto)
        {
            auto palette = hrz::palette::from_proto(*proto);
            config->style_palettes.push_back(std::move(palette));
        }

        config->rng_seed = rng_seed;

        bool style_is_valid = false;

        if (!style_script.empty())
        {
            style_is_valid = true;

            auto lexer = hrz::style::Lexer::create(style_script);
            auto parser = hrz::style::Parser::create();

            for (auto& attribute : config->attributes)
            {
                auto style_id = parser->add_attribute(attribute.name);

                if (style_id == hrz::style::Parser::INSERT_ERROR)
                {
                    HRZ_LOG_WARNING("Couldn't register attribute \"{}\".", attribute.name);
                    style_is_valid = false;
                }

                attribute.style_id = style_id;
            }

            parser->add_property(COLOR_PRP, "color");

            _tile_depth_uniform_id = (uint32_t)parser->add_uniform("tile_depth");

            for (const auto& palette : config->style_palettes)
            {
                parser->add_palette(palette.name.c_str());
            }

            hrz::style::Ast full_ast;
            auto parse_result = parser->parse(*lexer, full_ast);
            if (!parse_result)
            {
                HRZ_LOG_ERROR(
                    "Styling script parsing error {} ({}) at line {}", parse_result.description(),
                    fmt::underlying(parse_result.type), parse_result.line);
                config->style_ast = nullptr;
                style_is_valid = false;
            }
            else
            {
                auto optimizer = hrz::style::Optimizer::create(config->style_palettes);
                if (!optimizer->optimize(std::move(full_ast), config->style_ast.get()))
                {
                    HRZ_LOG_ERROR("Styling script optimization error");
                    config->style_ast = nullptr;
                    style_is_valid = false;
                }
            }
        }

        config->has_style = style_is_valid;

        _trigger_restyling(tileset, false);
    }

    void _cancel_downloads(TilesetH handle, hrz::AssetsLoader* al)
    {
        auto tileset = _tilesets.get_object(handle);

        for (auto& tile : tileset->tiles)
        {
            for (auto& subtile : tile.subtiles)
            {
                if (subtile.has_external_tileset())
                {
                    auto& content = std::get<ThreeDTile::TilesetContent>(subtile.content);
                    if (content.tileset_handle != 0)
                    {
                        _cancel_downloads(content.tileset_handle, al);
                    }
                }
            }
        }

        bool tileset_is_loaded = _loaded_tilesets.find(handle) != _loaded_tilesets.end();
        if (tileset_is_loaded)
        {
            for (auto& tile_index : tileset->active_tiles)
            {
                auto& tile = tileset->tiles.at(tile_index);

                if (tile.load_status == ThreeDTile::LoadStatus::LOADING_TILE)
                {
                    hrz::assets_loader::end(al, tile.load_data_ticket);
                    tile.load_status = ThreeDTile::LoadStatus::UNLOADED;

                    if (tile_index != tileset->root_tile_index)
                    {
                        auto& parent_tile = tileset->tiles.at(tile.parent_index);
                        assert(parent_tile.active_child_count >= 1);
                        parent_tile.active_child_count -= 1;
                    }
                }

                for (auto& subtile : tile.subtiles)
                {
                    if (subtile.styling_status
                        == ThreeDTile::StylingStatus::WAITING_FOR_ATTRIBUTE_VALUES)
                    {
                        _vector_data_channel.send(hrz::vector_data::messages::ReleaseDataRequest{
                            subtile.vector_data_attribute_request_id});
                        subtile.vector_data_attribute_status =
                            ThreeDTile::Subtile::VectorDataAttributeStatus::UNREQUESTED;
                        subtile.styling_status = ThreeDTile::StylingStatus::IDLE;
                    }
                }
            }
        }
    }

    void set_visibility(TilesetH handle, bool is_visible, hrz::AssetsLoader* al)
    {
        auto tileset = _get_tileset(handle);
        if (tileset == nullptr) return;

        tileset->config->is_visible = is_visible;

        if (!is_visible)
        {
            _cancel_downloads(handle, al);
        }
    }

    void set_visibility_constraints(
        TilesetH handle,
        const hrz_proto::LayerVisibilityConstraintList& visibility_constraints)
    {
        auto tileset = _get_tileset(handle);
        if (tileset == nullptr) return;

        tileset->config->visibility_constraints = visibility_constraints;
    }

    bool is_tileset_visible(TilesetH handle)
    {
        auto tileset = _tilesets.get_object(handle);

        if (tileset == nullptr)
        {
            HRZ_LOG_WARNING("Unknown tileset handle: {}", handle);
            return false;
        }

        return tileset->config->is_visible
            && tileset->config->visibility_constraints_result.satisfied_in;
    }

    void set_max_screen_space_error(TilesetH handle, double max_screen_space_error)
    {
        auto tileset = _get_tileset(handle);
        if (tileset == nullptr) return;

        // Avoid dividing by 0 later-on.
        tileset->config->max_screen_space_error = std::max(0.01, max_screen_space_error);
    }

    void set_refinement_hysteresis(TilesetH handle, double refinement_hysteresis)
    {
        auto tileset = _get_tileset(handle);
        if (tileset == nullptr) return;

        tileset->config->screen_space_error_hysteresis =
            compute_screen_space_error_hysteresis(refinement_hysteresis);
    }

    void set_loading_priority(TilesetH handle, int8_t loading_priority)
    {
        auto tileset = _get_tileset(handle);
        if (tileset == nullptr) return;

        tileset->config->loading_priority = loading_priority;
    }

    void set_scene_views_bitset(TilesetH handle, uint32_t bitset)
    {
        auto tileset = _get_tileset(handle);
        if (tileset == nullptr) return;

        tileset->config->scene_views_bitset = bitset;
    }

    // Returns true if the headers change might change the content.
    bool set_http_headers(TilesetH handle, const hrz::HttpHeaders& headers)
    {
        auto tileset = _get_tileset(handle);
        if (tileset == nullptr) return false;

        auto old = std::exchange(tileset->config->headers, headers);
        return old.hash_content() != tileset->config->headers.hash_content();
    }

    struct RecreateAllMaterialsVisitor
    {
        void operator()(Tileset& tileset, ThreeDTile&, ThreeDTile::B3dmContent& b3d_model) const
        {
            if (b3d_model.prototype)
            {
                b3d_model.materials.recreate_all_materials(
                    b3d_model.prototype, tileset.config->materials);
            }
        }

        void operator()(Tileset& tileset, ThreeDTile&, ThreeDTile::I3dmContent& i3d_model) const
        {
            // No-op
        }

        void operator()(Tileset& tileset, ThreeDTile&, ThreeDTile::PntsContent& pnts) const
        {
            // No-op
        }
    };

    struct AddMaterialVisitor
    {
        const hrz_proto::Material& material;

        void operator()(Tileset& tileset, ThreeDTile&, ThreeDTile::B3dmContent& b3d_model) const
        {
            if (b3d_model.prototype)
            {
                b3d_model.materials.add_material(material);
            }
        }

        void operator()(Tileset& tileset, ThreeDTile&, ThreeDTile::I3dmContent& i3d_model) const
        {
            // No-op
        }

        void operator()(Tileset& tileset, ThreeDTile&, ThreeDTile::PntsContent& pnts) const
        {
            // No-op
        }
    };

    struct RemoveMaterialVisitor
    {
        size_t index;

        void operator()(Tileset& tileset, ThreeDTile&, ThreeDTile::B3dmContent& b3d_model) const
        {
            assert(index < b3d_model.materials.material_count());

            if (b3d_model.prototype)
            {
                b3d_model.materials.remove_material(b3d_model.prototype, index);
            }
        }

        void operator()(Tileset& tileset, ThreeDTile&, ThreeDTile::I3dmContent& i3d_model) const
        {
            // No-op
        }

        void operator()(Tileset& tileset, ThreeDTile&, ThreeDTile::PntsContent& pnts) const
        {
            // No-op
        }
    };

    struct UpdateMaterialVisitor
    {
        size_t index;
        const hrz_proto::Material& material;

        void operator()(Tileset& tileset, ThreeDTile&, ThreeDTile::B3dmContent& b3d_model) const
        {
            if (b3d_model.prototype)
            {
                b3d_model.materials.update_whole_material(b3d_model.prototype, material, index);
            }
        }

        void operator()(Tileset& tileset, ThreeDTile&, ThreeDTile::I3dmContent& i3d_model) const
        {
            // No-op
        }

        void operator()(Tileset& tileset, ThreeDTile&, ThreeDTile::PntsContent& pnts) const
        {
            // No-op
        }
    };

    struct UpdateMaterialPaletteVisitor
    {
        size_t index;
        const hrz_proto::NumericPalette& palette;

        void operator()(Tileset& tileset, ThreeDTile&, ThreeDTile::B3dmContent& b3d_model) const
        {
            if (b3d_model.prototype)
            {
                b3d_model.materials.update_material_palette(b3d_model.prototype, palette, index);
            }
        }

        void operator()(Tileset& tileset, ThreeDTile&, ThreeDTile::I3dmContent& i3d_model) const
        {
            // No-op
        }

        void operator()(Tileset& tileset, ThreeDTile&, ThreeDTile::PntsContent& pnts) const
        {
            // No-op
        }
    };

    struct UpdateActiveMaterialsVisitor
    {
        void operator()(Tileset& tileset, ThreeDTile&, ThreeDTile::B3dmContent& b3d_model) const
        {
            if (b3d_model.prototype)
            {
                b3d_model.materials.set_active_materials(
                    tileset.config->base_material,
                    tileset.config->inherited_draw_prps.overlay_material_enabled
                        ? tileset.config->overlay_material
                        : std::optional<std::string_view>(std::nullopt));
            }
        }

        void operator()(Tileset& tileset, ThreeDTile&, ThreeDTile::I3dmContent& i3d_model) const
        {
            // No-op
        }

        void operator()(Tileset& tileset, ThreeDTile&, ThreeDTile::PntsContent& pnts) const
        {
            // No-op
        }
    };

    struct UpdateSelectedFeaturesVisitor
    {
        void operator()(Tileset& tileset, ThreeDTile&, ThreeDTile::B3dmContent& b3d_model) const
        {
            if (b3d_model.prototype && b3d_model.geometry)
            {
                hrz::model::set_batched_selection(
                    b3d_model.prototype, *b3d_model.geometry, tileset.config->selected_feature_ids);
            }
        }

        void operator()(Tileset& tileset, ThreeDTile&, ThreeDTile::I3dmContent& i3d_model) const
        {
            if (i3d_model.prototype && i3d_model.instance_group.has_value())
            {
                hrz::model::set_instance_group_selection(
                    i3d_model.prototype, i3d_model.instance_group.value(),
                    tileset.config->selected_feature_ids);
            }
        }

        void operator()(Tileset& tileset, ThreeDTile&, ThreeDTile::PntsContent& pnts) const
        {
            if (pnts.point_cloud)
            {
                pnts.point_cloud->update_selection(tileset.config->selected_feature_ids);
            }
        }
    };

    struct UpdateMaterialPropertiesVisitor
    {
        void operator()(Tileset& tileset, ThreeDTile&, ThreeDTile::B3dmContent& b3d_model) const
        {
            b3d_model.draw_prps.feature_color_blend_mode =
                tileset.config->inherited_draw_prps.feature_color_blend_mode;
            b3d_model.draw_prps.feature_color_blend_strength =
                tileset.config->inherited_draw_prps.feature_color_blend_strength;
            b3d_model.draw_prps.apply_feature_color_to_overlay =
                tileset.config->inherited_draw_prps.apply_feature_color_to_overlay;
            b3d_model.draw_prps.overlay_material_enabled =
                tileset.config->inherited_draw_prps.overlay_material_enabled;
            b3d_model.draw_prps.overlay_material_opacity =
                tileset.config->inherited_draw_prps.overlay_material_opacity;
        }

        void operator()(Tileset& tileset, ThreeDTile&, ThreeDTile::I3dmContent& i3d_model) const
        {
            i3d_model.draw_prps.feature_color_blend_mode =
                tileset.config->inherited_draw_prps.feature_color_blend_mode;
            i3d_model.draw_prps.feature_color_blend_strength =
                tileset.config->inherited_draw_prps.feature_color_blend_strength;
        }

        void operator()(Tileset& tileset, ThreeDTile&, ThreeDTile::PntsContent& pnts) const
        {
            pnts.update_appearance(tileset.config->inherited_draw_prps);
        }
    };

    struct UpdateAppearanceVisitor
    {
        void operator()(Tileset& tileset, ThreeDTile&, ThreeDTile::B3dmContent& b3d_model) const
        {
            b3d_model.draw_prps.clip_id = tileset.config->inherited_draw_prps.clip_id;
            b3d_model.draw_prps.lighting = tileset.config->inherited_draw_prps.lighting;
            b3d_model.draw_prps.draw_under_flat_overlays =
                tileset.config->inherited_draw_prps.draw_under_flat_overlays;
        }

        void operator()(Tileset& tileset, ThreeDTile&, ThreeDTile::I3dmContent& i3d_model) const
        {
            i3d_model.draw_prps.clip_id = tileset.config->inherited_draw_prps.clip_id;
            i3d_model.draw_prps.lighting = tileset.config->inherited_draw_prps.lighting;
            i3d_model.draw_prps.draw_under_flat_overlays =
                tileset.config->inherited_draw_prps.draw_under_flat_overlays;
        }

        void operator()(Tileset& tileset, ThreeDTile&, ThreeDTile::PntsContent& pnts) const
        {
            pnts.update_appearance(tileset.config->inherited_draw_prps);
        }
    };

    template<typename VISITOR>
    void visit_tiles(TilesetH handle, const VISITOR& visitor)
    {
        auto tileset = _tilesets.get_object(handle);
        if (tileset == nullptr) return;

        for (auto& tile : tileset->tiles)
        {
            for (auto& subtile : tile.subtiles)
            {
                std::visit(
                    [&]<typename T>(T& content)
                    {
                        if constexpr (std::is_same_v<T, ThreeDTile::TilesetContent>)
                        {
                            visit_tiles<VISITOR>(content.tileset_handle, visitor);
                        }
                        else if constexpr (!std::is_same_v<T, std::monostate>)
                        {
                            visitor(*tileset, tile, content);
                        }
                    },
                    subtile.content);
            }
        }
    }

    void recreate_all_materials(
        TilesetH handle,
        std::span<const hrz_proto::Material* const> materials)
    {
        auto tileset = _get_tileset(handle);
        if (tileset == nullptr) return;

        auto* config = tileset->config;

        config->materials.resize(materials.size());
        for (size_t i = 0; i < materials.size(); ++i)
        {
            config->materials[i] = *materials[i];
        }

        visit_tiles(handle, RecreateAllMaterialsVisitor{});
    }

    void add_material(TilesetH handle, const hrz_proto::Material& material)
    {
        auto tileset = _get_tileset(handle);
        if (tileset == nullptr) return;

        auto* config = tileset->config;

        config->materials.push_back(material);
        visit_tiles(handle, AddMaterialVisitor{material});
    }

    void remove_material(TilesetH handle, size_t index)
    {
        auto tileset = _get_tileset(handle);
        if (tileset == nullptr) return;

        auto* config = tileset->config;

        assert(index < config->materials.size());
        config->materials.erase(config->materials.begin() + index);

        visit_tiles(handle, RemoveMaterialVisitor{index});
    }

    void update_material(TilesetH handle, size_t index, const hrz_proto::Material& material)
    {
        auto tileset = _get_tileset(handle);
        if (tileset == nullptr) return;

        auto* config = tileset->config;

        assert(index < config->materials.size());
        config->materials[index] = material;

        visit_tiles(handle, UpdateMaterialVisitor{index, material});
    }

    void update_material_palette(
        TilesetH handle,
        size_t index,
        const hrz_proto::NumericPalette& palette)
    {
        auto tileset = _get_tileset(handle);
        if (tileset == nullptr) return;

        auto* config = tileset->config;

        assert(index < config->materials.size());
        *config->materials[index].mutable_data_texture_palette() = palette;

        visit_tiles(handle, UpdateMaterialPaletteVisitor{index, palette});
    }

    bool reload_subtiles_content(TilesetH handle, bool allow_external_tilesets = false)
    {
        auto tileset = _get_tileset(handle, allow_external_tilesets);
        if (tileset == nullptr) return false;

        bool is_reloading = false;

        for (size_t tile_index = 0; tile_index < tileset->tiles.size(); ++tile_index)
        {
            auto& tile = tileset->tiles[tile_index];
            bool has_subtile_to_load = false;

            for (auto& subtile : tile.subtiles)
            {
                std::visit(
                    hrz::overload{
                        [this, &subtile, &tile,
                         &has_subtile_to_load](ThreeDTile::TilesetContent& content)
                        {
                            if (reload_subtiles_content(content.tileset_handle, true))
                            {
                                subtile.load_status = ThreeDTile::Subtile::LoadStatus::LOADING;
                                tile.subtiles_left_to_load += 1;
                                has_subtile_to_load = true;
                            }
                        },
                        [&subtile, &tile, &has_subtile_to_load](ThreeDTile::B3dmContent&)
                        {
                            subtile.load_status = ThreeDTile::Subtile::LoadStatus::LOADING;
                            tile.subtiles_left_to_load += 1;
                            has_subtile_to_load = true;
                        },
                        [](ThreeDTile::I3dmContent&)
                        {
                            // We do nothing, because reloading happens for materials,
                            // but i3dm and pnts don't use dynamic materials.
                        },
                        [](ThreeDTile::PntsContent&)
                        {
                            // We do nothing, because reloading happens for materials,
                            // but i3dm and pnts don't use dynamic materials.
                        },
                        [](std::monostate&)
                        {
                            // We do nothing, because reloading happens for materials,
                            // but i3dm and pnts don't use dynamic materials.
                        }},
                    subtile.content);
            }

            if (has_subtile_to_load
                && (tile.load_status == ThreeDTile::LoadStatus::LOADED
                    || tile.load_status == ThreeDTile::LoadStatus::RENDERABLE
                    || tile.load_status == ThreeDTile::LoadStatus::ERROR))
            {
                // If a tile is in error, but reached this point, it means it has subtiles,
                // so its load error is beyond loading the tile itself, and we can try
                // reloading the subtiles.
                tile.load_status = ThreeDTile::LoadStatus::LOADING_CONTENT;
                is_reloading = true;
            }
        }

        return is_reloading;
    }

    void update_appearance(
        TilesetH handle,
        int8_t clip_id,
        const hrz::render::LightingSettings& lighting_settings,
        bool draw_under_flat_overlays)
    {
        auto tileset = _get_tileset(handle);
        if (tileset == nullptr) return;

        tileset->config->inherited_draw_prps.clip_id = clip_id;
        tileset->config->inherited_draw_prps.lighting = lighting_settings;
        tileset->config->inherited_draw_prps.draw_under_flat_overlays = draw_under_flat_overlays;

        visit_tiles(handle, UpdateAppearanceVisitor{});
    }

    void update_material_properties(TilesetH handle, const hrz_proto::MaterialProperties& prps)
    {
        auto tileset = _get_tileset(handle);
        if (tileset == nullptr) return;

        tileset->config->inherited_draw_prps.apply_feature_color_to_overlay =
            prps.apply_feature_color_to_overlay();
        tileset->config->inherited_draw_prps.overlay_material_enabled = prps.enable_overlay();
        tileset->config->inherited_draw_prps.overlay_material_opacity = prps.overlay_opacity();
        tileset->config->inherited_draw_prps.feature_color_blend_mode =
            prps.feature_color_blend_mode();
        tileset->config->inherited_draw_prps.feature_color_blend_strength =
            prps.feature_color_blend_strength();

        visit_tiles(handle, UpdateMaterialPropertiesVisitor{});
    }

    void update_active_materials(
        TilesetH handle,
        std::string_view base,
        std::optional<std::string_view> overlay)
    {
        auto tileset = _get_tileset(handle);
        if (tileset == nullptr) return;

        tileset->config->base_material = std::string(base);
        if (overlay) tileset->config->overlay_material = std::string(overlay.value());

        visit_tiles(handle, UpdateActiveMaterialsVisitor{});
    }

    void add_selected_features(
        TilesetH handle,
        std::span<const hrz::vector_data::FeatureIdHash> feature_ids)
    {
        auto tileset = _get_tileset(handle);
        if (tileset == nullptr) return;

        for (auto feature_id : feature_ids)
        {
            // feature_id == 0 represents an empty feature ID. If the layer
            // has no feature IDs, we don't want to select and highlight the
            // whole layer.
            if (feature_id != 0)
            {
                tileset->config->selected_feature_ids.insert(feature_id);
            }
        }

        visit_tiles(handle, UpdateSelectedFeaturesVisitor{});
    }

    void clear_selected_features(TilesetH handle)
    {
        auto tileset = _get_tileset(handle);
        if (tileset == nullptr) return;

        tileset->config->selected_feature_ids.clear();
        visit_tiles(handle, UpdateSelectedFeaturesVisitor{});
    }

    void remove_tileset(TilesetH handle)
    {
        _loading_tilesets.erase(handle);
        _loaded_tilesets.erase(handle);
        _removed_tilesets.insert(handle);
    }

    hrz::picking::ObjectReference make_object_reference(
        TilesetH tileset_handle,
        uint32_t tile_index) const
    {
        hrz::picking::ObjectReference obj;
        obj.system_id = _system_picking_id;
        obj.complementary_id = (tileset_handle << 10) + (tile_index >> 14);
        obj.object_id = ((tile_index & 0x3fff) << 18);
        return obj;
    }

    static void extract_info_from_object_reference(
        const hrz::picking::ObjectReference& ref,
        TilesetH* out_tileset_handle,
        uint32_t* out_tile_index,
        uint32_t* out_batch_id)
    {
        *out_tileset_handle = (ref.complementary_id & 0xfffc00) >> 10;
        *out_tile_index =
            ((ref.complementary_id & 0x3ff) << 14) + ((ref.object_id & 0xfffc0000) >> 18);
        *out_batch_id = ref.object_id & 0x3ffff;
    }

    void _unload_b3dm_subtile(
        ThreeDTile::B3dmContent& content,
        hrz::AssetsLoader* al,
        hrz::JobScheduler* js,
        hrz::BlobAllocator* ba)
    {
        if (content.prototype)
        {
            content.materials.delete_all(content.prototype);
            if (content.geometry)
            {
                hrz::model::destroy(content.prototype, *content.geometry);
            }
            hrz::model::destroy(content.prototype, al, js, ba, _removed_resources);
            content.prototype = nullptr;
        }

        hrz_jobs::cancel_job(js, content.decode_batch_table_ticket);

        content.per_batch_color.clear();
        content.per_batch_color.shrink_to_fit();
        content.batch_length = 0;
    }

    void _unload_i3dm_subtile(
        ThreeDTile::I3dmContent& content,
        hrz::AssetsLoader* al,
        hrz::JobScheduler* js,
        hrz::BlobAllocator* ba)
    {
        if (content.prototype && content.instance_group)
        {
            hrz::model::destroy(content.prototype, *content.instance_group);
            content.instance_group = std::nullopt;
        }

        if (content.model_uri_hash.has_value())
        {
            auto it = _model_uris_to_prototypes.find(content.model_uri_hash.value());
            if (it != std::end(_model_uris_to_prototypes))
            {
                auto& proto_ref = it->second;

                assert(proto_ref.ref_count > 0);
                proto_ref.ref_count -= 1;
            }
            else
            {
                assert(false && "Could not find shared model in the cache");
            }

            content.model_uri_hash = std::nullopt;
        }
        else if (content.prototype)
        {
            // Non-external models are not put in the cache, free them directly

            if (content.geometry)
            {
                hrz::model::destroy(content.prototype, *content.geometry);
            }

            if (content.model)
            {
                hrz::model::destroy(content.prototype, *content.model);
            }

            if (content.material)
            {
                hrz::model::destroy(content.prototype, *content.material);
            }

            hrz::model::destroy(content.prototype, al, js, ba, _removed_resources);
        }

        content.geometry = std::nullopt;
        content.model = std::nullopt;
        content.material = std::nullopt;
        content.prototype = nullptr;
        content.batch_length = 0;
        content.instances_length = 0;

        hrz_jobs::cancel_job(js, content.decode_batch_table_ticket);

        content.position_data.clear();
        content.position_data.shrink_to_fit();
        content.position_quantized_data.clear();
        content.position_quantized_data.shrink_to_fit();
        content.normal_data.clear();
        content.normal_data.shrink_to_fit();
        content.normal_oct32p_data.clear();
        content.normal_oct32p_data.shrink_to_fit();
        content.scale_data.clear();
        content.scale_data.shrink_to_fit();
        content.batch_id_data.clear();
        content.batch_id_data.shrink_to_fit();
        content.per_instance_color.clear();
        content.per_instance_color.shrink_to_fit();
    }

    void _unload_pnts_subtile(
        ThreeDTile::PntsContent& content,
        hrz::AssetsLoader* al,
        hrz::JobScheduler* js,
        hrz::BlobAllocator* ba)
    {
        content.geometry = std::nullopt;

        if (content.point_cloud)
        {
            content.point_cloud->destroy(_removed_resources);
            content.point_cloud.reset();
        }

        content.batch_length = 0;
        content.per_batch_color.clear();
        content.per_batch_color.shrink_to_fit();

        hrz_jobs::cancel_job(js, content.decode_batch_table_ticket);
    }

    void _unload_subtile(
        ThreeDTile::Subtile& subtile,
        hrz::AssetsLoader* al,
        hrz::JobScheduler* js,
        hrz::BlobAllocator* ba)
    {
        std::visit(
            hrz::overload{
                [this, al, js, ba](ThreeDTile::B3dmContent& content)
                { _unload_b3dm_subtile(content, al, js, ba); },
                [this, al, js, ba](ThreeDTile::I3dmContent& content)
                { _unload_i3dm_subtile(content, al, js, ba); },
                [this, al, js, ba](ThreeDTile::PntsContent& content)
                { _unload_pnts_subtile(content, al, js, ba); },
                [this, al, js, ba](ThreeDTile::TilesetContent& content)
                {
                    if (content.tileset_handle != 0)
                    {
                        _unload_tileset(content.tileset_handle, al, js, ba);
                    }
                    content.tileset_handle = 0;
                },
                [](std::monostate&) {},
            },
            subtile.content);

        _vector_data_channel.send(hrz::vector_data::messages::ReleaseDataRequest{
            subtile.vector_data_attribute_request_id});
        subtile.vector_data_attribute_status =
            ThreeDTile::Subtile::VectorDataAttributeStatus::UNREQUESTED;

        hrz_jobs::cancel_job(js, subtile.style_job_ticket);
        subtile.styling_status = ThreeDTile::StylingStatus::IDLE;

        subtile.batch_id_offset = 0;
        subtile.batches_to_feature_ids = {};
        subtile.attribute_values.clear();
        subtile.vector_data_attribute_request_id = 0;
        subtile.has_received_vector_data_attributes_once = false;
    }

    void _unload_tile(
        Tileset* tileset,
        ThreeDTile& tile,
        hrz::AssetsLoader* al,
        hrz::JobScheduler* js,
        hrz::BlobAllocator* ba)
    {
        HRZ_SCOPED_SAMPLE_A("unload tile");

        for (auto& subtile : tile.subtiles)
        {
            _unload_subtile(subtile, al, js, ba);
        }
        tile.subtiles.clear();

        tile.was_made_renderable_once = false;

        hrz::assets_loader::end(al, tile.load_data_ticket);

#if DRAW_DEBUG_BOXES
        if (tile.renderable_box.has_value())
        {
            auto& renderable = tile.renderable_box.value();
            _removed_resources.push_back(renderable.data.vertex_input);
            _removed_resources.push_back(renderable.data.ubo_buffer);
            _removed_resources.push_back(renderable.pos_normal_buffer);
            _removed_resources.push_back(renderable.color_buffer);
            tile.renderable_box = std::nullopt;
        }
#endif

        tile.load_status = ThreeDTile::LoadStatus::UNLOADED;
    }

    void _unload_tileset(
        TilesetH handle,
        hrz::AssetsLoader* al,
        hrz::JobScheduler* js,
        hrz::BlobAllocator* ba)
    {
        HRZ_SCOPED_SAMPLE("unload tileset");

        _loading_tilesets.erase(handle);
        _loaded_tilesets.erase(handle);

        auto tileset = _tilesets.get_object(handle);
        if (!tileset) return;

        hrz::assets_loader::end(al, tileset->load_descriptor_ticket);
        hrz_jobs::cancel_job(js, tileset->decode_descriptor_ticket);

        for (auto& tile : tileset->tiles)
        {
            _unload_tile(tileset, tile, al, js, ba);
        }

        if (!tileset->is_external)
        {
            auto config = tileset->config;

            _vector_data_channel.send(hrz::vector_data::messages::ReleaseLayerLoader{
                config->vector_data_layer_request_id});

            delete config;
        }

        _tilesets.release(handle);
    }

    bool _decode_b3dm_feature_table(
        const ThreeDTile& tile,
        ThreeDTile::Subtile* subtile,
        std::span<const std::byte> feature_table_json_data,
        std::span<const std::byte> /*feature_table_bin_data*/) const
    {
        HRZ_SCOPED_SAMPLE("decode b3dm feature table");

        auto& content = std::get<ThreeDTile::B3dmContent>(subtile->content);

        // Binary body is not used with b3dm tiles.

        if (feature_table_json_data.empty())
        {
            return true;
        }

        rapidjson::Document document;
        document.Parse((const char*)feature_table_json_data.data(), feature_table_json_data.size());

        if (document.HasParseError())
        {
            HRZ_LOG_ERROR(
                "Could not parse feature table JSON of tile \"{}\": {}", tile.uri,
                rapidjson::GetParseError_En(document.GetParseError()));
            return false;
        }

        if (!document.IsObject())
        {
            HRZ_LOG_ERROR("Invalid feature table JSON in tile \"{}\"", tile.uri);
            return false;
        }

        content.batch_length = hrz::json::get_int_or(document, "BATCH_LENGTH", 0);

        if (document.HasMember("RTC_CENTER"))
        {
            const auto& rtc_center_node = document["RTC_CENTER"];
            if (rtc_center_node.IsArray() && rtc_center_node.Size() == 3)
            {
                lm::dvec3 rtc_center;
                bool valid = true;

                const auto& array = rtc_center_node.GetArray();
                for (unsigned int i = 0; i < 3; ++i)
                {
                    const auto& entry = array[i];
                    if (!entry.IsNumber())
                    {
                        valid = false;
                        break;
                    }

                    rtc_center.m[i] = entry.GetDouble();
                }

                if (valid)
                {
                    subtile->rtc_transform = lm::translation(rtc_center);
                }
            }
        }

        return true;
    }

    template<typename SRC, typename DST>
    static void _load_binary_data(std::span<const SRC> src, std::span<DST> dst)
    {
        assert(src.size() == dst.size());
        assert(sizeof(DST) >= sizeof(SRC));
        std::ranges::copy_n(src.begin(), src.size(), dst.begin());
    }

    bool _decode_i3dm_feature_table(
        const ThreeDTile& tile,
        ThreeDTile::Subtile* subtile,
        std::span<const std::byte> feature_table_json_data,
        std::span<const std::byte> feature_table_bin_data)
    {
        HRZ_SCOPED_SAMPLE("decode i3dm feature table");

        if (feature_table_json_data.empty())
        {
            return true;
        }

        auto& content = std::get<ThreeDTile::I3dmContent>(subtile->content);

        rapidjson::Document document;
        document.Parse((const char*)feature_table_json_data.data(), feature_table_json_data.size());

        if (document.HasParseError())
        {
            HRZ_LOG_ERROR(
                "Could not parse feature table JSON of tile \"{}\": {}", tile.uri,
                rapidjson::GetParseError_En(document.GetParseError()));
            return false;
        }

        if (!document.IsObject())
        {
            HRZ_LOG_ERROR("Invalid feature table JSON in tile \"{}\"", tile.uri);
            return false;
        }

        content.instances_length = hrz::json::get_int_or(document, "INSTANCES_LENGTH", 0);
        if (content.instances_length == 0)
        {
            return true;
        }

        content.use_east_north_up_orientation =
            hrz::json::get_bool_or(document, "EAST_NORTH_UP", false);

        if (document.HasMember("RTC_CENTER"))
        {
            lm::dvec3 rtc_center;
            if (hrz::json::copy_array_values(
                    std::span<double>(rtc_center.m), document["RTC_CENTER"])
                != 3)
            {
                HRZ_LOG_ERROR("Unexpected number of values in the \"RTC_CENTER\" array.");
                return false;
            }

            subtile->rtc_transform = lm::translation(rtc_center);
        }

#define CHECK_DATA_SIZE()                                                                       \
    do                                                                                          \
    {                                                                                           \
        if (byte_offset + data_size > feature_table_bin_data.size())                            \
        {                                                                                       \
            HRZ_LOG_ERROR(                                                                      \
                "Not enough data in feature table in tile \"{}\". Byte offset: {}, data size: " \
                "{}, feature table size: {}",                                                   \
                tile.uri, byte_offset, data_size, feature_table_bin_data.size());               \
            return false;                                                                       \
        }                                                                                       \
    } while (0)

        const auto& position_node = hrz::json::get_member_or_null(document, "POSITION");
        const auto& position_quantized_node =
            hrz::json::get_member_or_null(document, "POSITION_QUANTIZED");

        if (position_node.IsNull() && position_quantized_node.IsNull())
        {
            HRZ_LOG_ERROR(
                "I3dm feature table is missing features positions in tile \"{}\"", tile.uri);
            return false;
        }
        else if (!position_node.IsNull())
        {
            auto byte_offset = hrz::json::get_int_or(position_node, "byteOffset", 0);
            size_t data_size = content.instances_length * sizeof(lm::vec3);
            CHECK_DATA_SIZE();

            content.position_data.resize(content.instances_length);
            std::memcpy(
                content.position_data.data(),
                (const lm::vec3*)(feature_table_bin_data.data() + byte_offset), data_size);
        }
        else if (!position_quantized_node.IsNull())
        {
            if (!document.HasMember("QUANTIZED_VOLUME_OFFSET"))
            {
                HRZ_LOG_ERROR(
                    "Could not use quantized position because the quantized volume offset is not "
                    "defined in i3dm tile \"{}\"",
                    tile.uri);
                return false;
            }

            if (!document.HasMember("QUANTIZED_VOLUME_SCALE"))
            {
                HRZ_LOG_ERROR(
                    "Could not use quantized position because the quantized volume offset is not "
                    "defined in i3dm tile \"{}\"",
                    tile.uri);
                return false;
            }

            if (hrz::json::copy_array_values(
                    std::span<double>(content.quantized_volume_offset.m),
                    document["QUANTIZED_VOLUME_OFFSET"])
                != 3)
            {
                HRZ_LOG_ERROR(
                    "Unexpected number of values in the \"QUANTIZED_VOLUME_OFFSET\" array.");
                return false;
            }

            if (hrz::json::copy_array_values(
                    std::span<double>(content.quantized_volume_scale.m),
                    document["QUANTIZED_VOLUME_SCALE"])
                != 3)
            {
                HRZ_LOG_ERROR(
                    "Unexpected number of values in the \"QUANTIZED_VOLUME_SCALE\" array.");
                return false;
            }

            auto byte_offset = hrz::json::get_int_or(position_quantized_node, "byteOffset", 0);
            size_t data_size = content.instances_length * sizeof(lm::usvec3);
            CHECK_DATA_SIZE();

            content.position_quantized_data.resize(content.instances_length);
            std::memcpy(
                content.position_quantized_data.data(),
                (const lm::usvec3*)(feature_table_bin_data.data() + byte_offset), data_size);
        }

        const auto& normal_up_node = hrz::json::get_member_or_null(document, "NORMAL_UP");
        const auto& normal_right_node = hrz::json::get_member_or_null(document, "NORMAL_RIGHT");
        const auto& normal_up_oct32p_node =
            hrz::json::get_member_or_null(document, "NORMAL_UP_OCT32P");
        const auto& normal_right_oct32p_node =
            hrz::json::get_member_or_null(document, "NORMAL_RIGHT_OCT32P");

        if ((!normal_up_node.IsNull() && normal_right_node.IsNull())
            || (!normal_right_node.IsNull() && normal_up_node.IsNull()))
        {
            HRZ_LOG_ERROR(
                "Missing either NORMAL_UP or NORMAL_RIGHT field in i3dm tile \"{}\"", tile.uri);
            return false;
        }
        else if (!normal_up_node.IsNull() && !normal_right_node.IsNull())
        {
            content.use_east_north_up_orientation = false;

            std::span<const lm::vec3> normals_right;
            std::span<const lm::vec3> normals_up;

            {
                auto byte_offset = hrz::json::get_int_or(normal_right_node, "byteOffset", 0);
                size_t data_size = content.instances_length * sizeof(lm::vec3);
                CHECK_DATA_SIZE();

                normals_right = {
                    (const lm::vec3*)(feature_table_bin_data.data() + byte_offset),
                    content.instances_length};
            }

            {
                auto byte_offset = hrz::json::get_int_or(normal_up_node, "byteOffset", 0);
                size_t data_size = content.instances_length * sizeof(lm::vec3);
                CHECK_DATA_SIZE();

                normals_up = {
                    (const lm::vec3*)(feature_table_bin_data.data() + byte_offset),
                    content.instances_length};
            }

            // Interleave right and up normals into a single array.
            content.normal_data.resize(2 * content.instances_length);
            for (uint32_t i = 0; i < content.instances_length; ++i)
            {
                content.normal_data[2 * i + 0] = normals_right[i];
                content.normal_data[2 * i + 1] = normals_up[i];
            }
        }
        else if (
            (!normal_up_oct32p_node.IsNull() && normal_right_oct32p_node.IsNull())
            || (!normal_right_oct32p_node.IsNull() && normal_up_oct32p_node.IsNull()))
        {
            HRZ_LOG_ERROR(
                "Missing either NORMAL_UP_OCT32P or NORMAL_RIGHT_OCT32P field in i3dm tile \"{}\"",
                tile.uri);
            return false;
        }
        else if (!normal_up_oct32p_node.IsNull() && !normal_right_oct32p_node.IsNull())
        {
            content.use_east_north_up_orientation = false;

            std::span<const lm::usvec2> normals_right_oct32p;
            std::span<const lm::usvec2> normals_up_oct32p;

            {
                auto byte_offset = hrz::json::get_int_or(normal_right_oct32p_node, "byteOffset", 0);
                size_t data_size = content.instances_length * sizeof(lm::usvec2);
                CHECK_DATA_SIZE();

                normals_right_oct32p = {
                    (const lm::usvec2*)(feature_table_bin_data.data() + byte_offset),
                    content.instances_length};
            }

            {
                auto byte_offset = hrz::json::get_int_or(normal_up_oct32p_node, "byteOffset", 0);
                size_t data_size = content.instances_length * sizeof(lm::usvec2);
                CHECK_DATA_SIZE();

                normals_up_oct32p = {
                    (const lm::usvec2*)(feature_table_bin_data.data() + byte_offset),
                    content.instances_length};
            }

            // Interleave right and up compressed normals into a single array.
            content.normal_oct32p_data.resize(content.instances_length);
            for (uint32_t i = 0; i < content.instances_length; ++i)
            {
                content.normal_oct32p_data[i].xy = normals_right_oct32p[i];
                content.normal_oct32p_data[i].zw = normals_up_oct32p[i];
            }
        }

        if (document.HasMember("SCALE_NON_UNIFORM"))
        {
            auto byte_offset =
                hrz::json::get_int_or(document["SCALE_NON_UNIFORM"], "byteOffset", 0);
            size_t data_size = content.instances_length * sizeof(lm::vec3);
            CHECK_DATA_SIZE();

            content.scale_data.resize(content.instances_length);
            std::memcpy(
                content.scale_data.data(),
                (const lm::vec3*)(feature_table_bin_data.data() + byte_offset), data_size);
        }
        else
        {
            // Use a default uniform scale of 1.
            content.scale_data.resize(content.instances_length, lm::vec3{1});
        }

        if (document.HasMember("SCALE"))
        {
            auto byte_offset = hrz::json::get_int_or(document["SCALE"], "byteOffset", 0);
            size_t data_size = content.instances_length * sizeof(float);
            CHECK_DATA_SIZE();

            std::span<const float> scales = {
                (const float*)(feature_table_bin_data.data() + byte_offset),
                content.instances_length};
            for (uint32_t i = 0; i < content.instances_length; ++i)
            {
                content.scale_data[i] *= scales[i];
            }
        }

        content.batch_id_data.resize(content.instances_length);

        if (document.HasMember("BATCH_ID"))
        {
            auto byte_offset = hrz::json::get_int_or(document["BATCH_ID"], "byteOffset", 0);

            auto component_type_str =
                hrz::json::get_str_or(document["BATCH_ID"], "componentType", "UNSIGNED_SHORT");
            auto component_type =
                hrz::three_d_tiles::component_type_from_string(component_type_str);
            assert(component_type.has_value());

            switch (component_type.value())
            {
                case hrz::three_d_tiles::AttributeComponentType::BYTE:
                case hrz::three_d_tiles::AttributeComponentType::UNSIGNED_BYTE:
                {
                    size_t data_size = content.instances_length * sizeof(uint8_t);
                    CHECK_DATA_SIZE();
                    _load_binary_data<uint8_t, uint32_t>(
                        {(const uint8_t*)(feature_table_bin_data.data() + byte_offset),
                         content.instances_length},
                        {content.batch_id_data.data(), content.instances_length});
                    break;
                }
                case hrz::three_d_tiles::AttributeComponentType::SHORT:
                case hrz::three_d_tiles::AttributeComponentType::UNSIGNED_SHORT:
                {
                    size_t data_size = content.instances_length * sizeof(uint16_t);
                    CHECK_DATA_SIZE();
                    _load_binary_data<uint16_t, uint32_t>(
                        {(const uint16_t*)(feature_table_bin_data.data() + byte_offset),
                         content.instances_length},
                        {content.batch_id_data.data(), content.instances_length});
                    break;
                }
                case hrz::three_d_tiles::AttributeComponentType::INT:
                case hrz::three_d_tiles::AttributeComponentType::UNSIGNED_INT:
                {
                    size_t data_size = content.instances_length * sizeof(uint32_t);
                    CHECK_DATA_SIZE();
                    _load_binary_data<uint32_t, uint32_t>(
                        {(const uint32_t*)(feature_table_bin_data.data() + byte_offset),
                         content.instances_length},
                        {content.batch_id_data.data(), content.instances_length});
                    break;
                }
                default:
                    HRZ_LOG_ERROR(
                        "Unexpected component type '{}' for 'BATCH_ID' semantic in i3dm tile "
                        "\"{}\"",
                        component_type_str, tile.uri);
                    return false;
            }

            content.batch_length = *std::ranges::max_element(content.batch_id_data) + 1;
        }
        else
        {
            // Not defined in the spec but Cesium's implementation says:
            // "If BATCH_ID semantic is undefined, batchId is just the instance number"
            // https://github.com/pmconne/cesium/blob/a4be986c04b863e97e251c3fc6b78f106f7de4d7/Source/Scene/Instanced3DModel3DTileContent.js#L433
            std::iota(std::begin(content.batch_id_data), std::end(content.batch_id_data), 0);
            content.batch_length = content.instances_length;
        }

#undef CHECK_DATA_SIZE

        return true;
    }

    static bool _decode_pnts_feature_table(
        const ThreeDTile& tile,
        ThreeDTile::Subtile* subtile,
        std::span<const std::byte> feature_table_json_data,
        hrz::blobs::BlobHandle feature_table_bin_blob,
        const hrz::monitoring::ResourceOwner& owner,
        hrz::BlobAllocator* ba)
    {
        HRZ_SCOPED_SAMPLE("decode pnts feature table");

        if (feature_table_json_data.empty())
        {
            return true;
        }

        auto feature_table_bin_data = feature_table_bin_blob.get_data();

        rapidjson::Document document;
        document.Parse((const char*)feature_table_json_data.data(), feature_table_json_data.size());

        if (document.HasParseError())
        {
            HRZ_LOG_ERROR(
                "Could not parse feature table JSON of tile \"{}\": {}", tile.uri,
                rapidjson::GetParseError_En(document.GetParseError()));
            return false;
        }

        if (!document.IsObject())
        {
            HRZ_LOG_ERROR("Invalid feature table JSON in tile \"{}\"", tile.uri);
            return false;
        }

        auto& content = subtile->content.emplace<ThreeDTile::PntsContent>();
        auto& geometry = content.geometry.emplace();

        content.batch_length = std::max(0, hrz::json::get_int_or(document, "BATCH_LENGTH", 0));
        geometry.batch_count = content.batch_length;

        geometry.point_count = hrz::json::get_int_or(document, "POINTS_LENGTH", 0);
        if (geometry.point_count == 0)
        {
            return true;
        }

        if (document.HasMember("RTC_CENTER"))
        {
            lm::dvec3 rtc_center;
            if (hrz::json::copy_array_values(
                    std::span<double>(rtc_center.m), document["RTC_CENTER"])
                != 3)
            {
                HRZ_LOG_ERROR("Unexpected number of values in the \"RTC_CENTER\" array.");
                return false;
            }

            subtile->rtc_transform = lm::translation(rtc_center);
        }

#define CHECK_DATA_SIZE()                                                                       \
    do                                                                                          \
    {                                                                                           \
        if (byte_offset + data_size > feature_table_bin_data.size())                            \
        {                                                                                       \
            HRZ_LOG_ERROR(                                                                      \
                "Not enough data in feature table in tile \"{}\". Byte offset: {}, data size: " \
                "{}, feature table size: {}",                                                   \
                tile.uri, byte_offset, data_size, feature_table_bin_data.size());               \
            return false;                                                                       \
        }                                                                                       \
    } while (0)

        if (document.HasMember("POSITION"))
        {
            const auto& position_node = hrz::json::get_member_or_null(document, "POSITION");

            auto byte_offset = hrz::json::get_int_or(position_node, "byteOffset", 0);
            size_t data_size = geometry.point_count * sizeof(lm::vec3);
            CHECK_DATA_SIZE();

            geometry.quantized_volume_offset = lm::dvec3(0.0f);
            geometry.quantized_volume_scale = lm::dvec3(1.0f);

            auto positions_opt = hrz::BlobArray<lm::vec3>::make_blob_array(
                ba,
                feature_table_bin_blob.make_sub_blob(
                    hrz::unsafe("Offset and size are checked above"), byte_offset, data_size));
            if (!positions_opt.has_value())
            {
                HRZ_LOG_ERROR("Incorrect alignment of position data in tile \"{}\"", tile.uri);
                return false;
            }

            geometry.positions = std::move(positions_opt.value());
            geometry.positions_format = my::VertexFormat::Float32_3;
        }
        else if (document.HasMember("POSITION_QUANTIZED"))
        {
            const auto& position_quantized_node =
                hrz::json::get_member_or_null(document, "POSITION_QUANTIZED");

            auto byte_offset = hrz::json::get_int_or(position_quantized_node, "byteOffset", 0);
            size_t data_size = geometry.point_count * sizeof(lm::usvec3);
            CHECK_DATA_SIZE();

            hrz::json::copy_array_values(
                std::span<double>(geometry.quantized_volume_offset.m),
                document["QUANTIZED_VOLUME_OFFSET"], 0.0);
            hrz::json::copy_array_values(
                std::span<double>(geometry.quantized_volume_scale.m),
                document["QUANTIZED_VOLUME_SCALE"], 1.0);

            auto positions_opt = hrz::BlobArray<lm::usvec3>::make_blob_array(
                ba,
                feature_table_bin_blob.make_sub_blob(
                    hrz::unsafe("Offset and size are checked above"), byte_offset, data_size));
            if (!positions_opt.has_value())
            {
                HRZ_LOG_ERROR("Incorrect alignment of position data in tile \"{}\"", tile.uri);
                return false;
            }

            geometry.positions = std::move(positions_opt.value());
            geometry.positions_format = my::VertexFormat::UInt16Norm_3;
        }
        else
        {
            HRZ_LOG_ERROR(
                "Pnts feature table is missing points positions in tile \"{}\"", tile.uri);
            return false;
        }

        if (document.HasMember("RGBA"))
        {
            const auto& rgba_node = hrz::json::get_member_or_null(document, "RGBA");

            auto byte_offset = hrz::json::get_int_or(rgba_node, "byteOffset", 0);
            size_t data_size = geometry.point_count * sizeof(lm::ubvec4);
            CHECK_DATA_SIZE();

            auto colors_opt = hrz::BlobArray<lm::ubvec4>::make_blob_array(
                ba,
                feature_table_bin_blob.make_sub_blob(
                    hrz::unsafe("Offset and size are checked above"), byte_offset, data_size));
            if (!colors_opt.has_value())
            {
                HRZ_LOG_ERROR("Incorrect alignment of color data in tile \"{}\"", tile.uri);
                return false;
            }

            {
                auto colors_data = colors_opt->get_data();
                for (lm::ubvec4 c : colors_data)
                {
                    if (c.a > 0 && c.a < 255)
                    {
                        geometry.has_transparent_color = true;
                        break;
                    }
                }
            }

            geometry.colors_rate = my::VertexRate::PerVertex;
            geometry.colors = std::move(colors_opt.value());
            geometry.colors_format = my::VertexFormat::UInt8Norm_4;
        }
        else if (document.HasMember("RGB"))
        {
            const auto& rgb_node = hrz::json::get_member_or_null(document, "RGB");

            auto byte_offset = hrz::json::get_int_or(rgb_node, "byteOffset", 0);
            size_t data_size = geometry.point_count * sizeof(lm::ubvec3);
            CHECK_DATA_SIZE();

            auto colors_opt = hrz::BlobArray<lm::ubvec3>::make_blob_array(
                ba,
                feature_table_bin_blob.make_sub_blob(
                    hrz::unsafe("Offset and size are checked above"), byte_offset, data_size));
            if (!colors_opt.has_value())
            {
                HRZ_LOG_ERROR("Incorrect alignment of color data in tile \"{}\"", tile.uri);
                return false;
            }

            geometry.has_transparent_color = false;
            geometry.colors_rate = my::VertexRate::PerVertex;
            geometry.colors = std::move(colors_opt.value());
            geometry.colors_format = my::VertexFormat::UInt8Norm_3;
        }
        else if (document.HasMember("RGB565"))
        {
            const auto& rgb565_node = hrz::json::get_member_or_null(document, "RGB565");

            auto byte_offset = hrz::json::get_int_or(rgb565_node, "byteOffset", 0);
            size_t data_size = geometry.point_count * sizeof(uint16_t);
            CHECK_DATA_SIZE();

            auto compressed_colors_opt = hrz::BlobArray<uint16_t>::make_blob_array(
                ba,
                feature_table_bin_blob.make_sub_blob(
                    hrz::unsafe("Offset and size are checked above"), byte_offset, data_size));
            if (!compressed_colors_opt.has_value())
            {
                HRZ_LOG_ERROR("Incorrect alignment of color data in tile \"{}\"", tile.uri);
                return false;
            }

            geometry.compressed_colors = {std::move(compressed_colors_opt.value())};
            geometry.decompressed_colors_allocation = {
                hrz::BlobArrayAllocation<lm::ubvec3>::allocate(ba, geometry.point_count)};

            // `geometry.colors` will be filled when the blob array allocation has finished.
        }
        else
        {
            lm::vec4 default_color(1.0f);

            hrz::json::copy_array_values(
                std::span<float>(default_color.m),
                hrz::json::get_member_or_null(document, "CONSTANT_RGBA"), 1.0f);

            geometry.has_transparent_color = default_color.a < 1.0f;
            geometry.colors_rate = my::VertexRate::Constant;
            geometry.colors = {hrz::convert_rgba_color_to_bytes(default_color)};
            geometry.colors_format = my::VertexFormat::UInt8Norm_4;
        }

        if (document.HasMember("NORMAL"))
        {
            const auto& normal_node = hrz::json::get_member_or_null(document, "NORMAL");

            auto byte_offset = hrz::json::get_int_or(normal_node, "byteOffset", 0);
            size_t data_size = geometry.point_count * sizeof(lm::vec3);
            CHECK_DATA_SIZE();

            auto normals_opt = hrz::BlobArray<lm::vec3>::make_blob_array(
                ba,
                feature_table_bin_blob.make_sub_blob(
                    hrz::unsafe("Offset and size are checked above"), byte_offset, data_size));
            if (!normals_opt.has_value())
            {
                HRZ_LOG_ERROR("Incorrect alignment of normal data in tile \"{}\"", tile.uri);
                return false;
            }

            geometry.normals = {std::move(normals_opt.value())};
            geometry.compressed_normals_allocation = {
                hrz::BlobArrayAllocation<uint16_t>::allocate(ba, geometry.point_count)};

            // `geometry.compressed_normals` will be filled when the blob array allocation has
            // finished.
        }
        else if (document.HasMember("NORMAL_OCT16P"))
        {
            const auto& rgba_node = hrz::json::get_member_or_null(document, "NORMAL_OCT16P");

            auto byte_offset = hrz::json::get_int_or(rgba_node, "byteOffset", 0);
            size_t data_size = geometry.point_count * sizeof(uint16_t);
            CHECK_DATA_SIZE();

            auto compressed_normals_opt = hrz::BlobArray<uint16_t>::make_blob_array(
                ba,
                feature_table_bin_blob.make_sub_blob(
                    hrz::unsafe("Offset and size are checked above"), byte_offset, data_size));
            if (!compressed_normals_opt.has_value())
            {
                HRZ_LOG_ERROR("Incorrect alignment of normal data in tile \"{}\"", tile.uri);
                return false;
            }

            geometry.compressed_normals_rate = my::VertexRate::PerVertex;
            geometry.compressed_normals = compressed_normals_opt.value();
        }
        else
        {
            geometry.compressed_normals_rate = my::VertexRate::Constant;
            geometry.compressed_normals =
                (uint16_t)hrz::octahedral_compress_normal<8>(lm::vec3{0, 0, 1});
        }

        if (content.batch_length > 0 && document.HasMember("BATCH_ID"))
        {
            auto byte_offset = hrz::json::get_int_or(document["BATCH_ID"], "byteOffset", 0);

            auto component_type_str =
                hrz::json::get_str_or(document["BATCH_ID"], "componentType", "UNSIGNED_SHORT");
            auto component_type =
                hrz::three_d_tiles::component_type_from_string(component_type_str);
            assert(component_type.has_value());

            switch (component_type.value())
            {
                case hrz::three_d_tiles::AttributeComponentType::UNSIGNED_BYTE:
                {
                    size_t data_size = geometry.point_count * sizeof(uint8_t);
                    CHECK_DATA_SIZE();

                    auto batch_ids_opt = hrz::BlobArray<uint8_t>::make_blob_array(
                        ba,
                        feature_table_bin_blob.make_sub_blob(
                            hrz::unsafe("Offset and size are checked above"), byte_offset,
                            data_size));
                    if (!batch_ids_opt.has_value())
                    {
                        HRZ_LOG_ERROR("Incorrect alignment of batch IDs in tile \"{}\"", tile.uri);
                        return false;
                    }

                    geometry.batch_ids = batch_ids_opt.value();
                    geometry.batch_ids_format = my::VertexFormat::UInt8;
                    geometry.batch_ids_rate = my::VertexRate::PerVertex;
                    break;
                }
                case hrz::three_d_tiles::AttributeComponentType::UNSIGNED_SHORT:
                {
                    size_t data_size = geometry.point_count * sizeof(uint16_t);
                    CHECK_DATA_SIZE();

                    auto batch_ids_opt = hrz::BlobArray<uint16_t>::make_blob_array(
                        ba,
                        feature_table_bin_blob.make_sub_blob(
                            hrz::unsafe("Offset and size are checked above"), byte_offset,
                            data_size));
                    if (!batch_ids_opt.has_value())
                    {
                        HRZ_LOG_ERROR("Incorrect alignment of batch IDs in tile \"{}\"", tile.uri);
                        return false;
                    }

                    geometry.batch_ids = batch_ids_opt.value();
                    geometry.batch_ids_format = my::VertexFormat::UInt16;
                    geometry.batch_ids_rate = my::VertexRate::PerVertex;
                    break;
                }
                case hrz::three_d_tiles::AttributeComponentType::UNSIGNED_INT:
                {
                    size_t data_size = geometry.point_count * sizeof(uint32_t);
                    CHECK_DATA_SIZE();

                    auto batch_ids_opt = hrz::BlobArray<uint32_t>::make_blob_array(
                        ba,
                        feature_table_bin_blob.make_sub_blob(
                            hrz::unsafe("Offset and size are checked above"), byte_offset,
                            data_size));
                    if (!batch_ids_opt.has_value())
                    {
                        HRZ_LOG_ERROR("Incorrect alignment of batch IDs in tile \"{}\"", tile.uri);
                        return false;
                    }

                    geometry.batch_ids = batch_ids_opt.value();
                    geometry.batch_ids_format = my::VertexFormat::UInt32;
                    geometry.batch_ids_rate = my::VertexRate::PerVertex;
                    break;
                }
                default:
                    HRZ_LOG_ERROR(
                        "Unexpected component type '{}' for 'BATCH_ID' semantic in pnts tile "
                        "\"{}\"",
                        component_type_str, tile.uri);
                    return false;
            }
        }
        else
        {
            geometry.batch_ids = 0u;
            geometry.batch_ids_format = my::VertexFormat::UInt32;
            geometry.batch_ids_rate = my::VertexRate::Constant;
        }

#undef CHECK_DATA_SIZE

        return true;
    }

    // Returns the previous lower (or equal) geometric error in the parent chain,
    // or return the tile's own geometric error if no lower value has been found.
    static double _get_lower_geometric_error_in_parent_tiles(Tileset* tileset, uint32_t tile_index)
    {
        const auto* tile = &tileset->tiles.at(tile_index);
        double geometric_error = tile->geometric_error;

        while (tile_index != tileset->root_tile_index)
        {
            const auto* parent_tile = &tileset->tiles.at(tile->parent_index);
            if (parent_tile->geometric_error <= geometric_error)
            {
                return parent_tile->geometric_error;
            }

            tile_index = tile->parent_index;
            tile = parent_tile;
        }

        if (tile_index == tileset->root_tile_index)
        {
            return std::min(tileset->geometric_error, geometric_error);
        }

        return geometric_error;
    }

    bool _load_b3dm(
        Tileset* tileset,
        ThreeDTile& tile,
        const hrz::blobs::BlobHandle& tile_data_blob,
        hrz::JobScheduler* js,
        hrz::BlobAllocator* ba,
        hrz::AttributionRegistry* attributions)
    {
        auto tile_data = tile_data_blob.get_data();
        const size_t tile_data_size = tile_data.size();

        auto magic = tile_data.subspan(0, 4);

        if (std::strncmp((const char*)magic.data(), "b3dm", 4) != 0) return false;

        const auto header_size = 28;

        if (tile_data_size < header_size)
        {
            HRZ_LOG_ERROR(
                "Not enough data received for b3dm tile \"{}\": {}", tile.uri, tile_data_size);
            return false;
        }

        auto header_data = tile_data.subspan(0, 28);

        auto format_version = read_uint32(header_data, 1);
        if (format_version != 1)
        {
            HRZ_LOG_ERROR("Unsupported b3dm version in tile \"{}\": {}", tile.uri, format_version);
            return false;
        }

        auto declared_size = read_uint32(header_data, 2);
        if (declared_size != tile_data_size)
        {
            HRZ_LOG_ERROR(
                "Wrong b3dm size in tile \"{}\": Expected {}, received {}", tile.uri, declared_size,
                tile_data_size);
            return false;
        }

        auto feature_table_json_size = read_uint32(header_data, 3);
        auto feature_table_bin_size = read_uint32(header_data, 4);
        auto batch_table_json_size = read_uint32(header_data, 5);
        auto batch_table_bin_size = read_uint32(header_data, 6);
        auto gltf_offset = header_size + feature_table_json_size + feature_table_bin_size
            + batch_table_json_size + batch_table_bin_size;

        if (gltf_offset > tile_data_size)
        {
            HRZ_LOG_ERROR("Invalid b3dm header in tile \"{}\"", tile.uri);
            return false;
        }

        auto config = tileset->config;

        ThreeDTile::Subtile subtile;
        subtile.content.emplace<ThreeDTile::B3dmContent>();

        subtile.vector_data_attribute_request_id =
            ((uint64_t)tileset->vector_data_attribute_request_id << 32)
            + ((uint64_t)tile.index << 8) + tile.subtiles.size();

        subtile.attribute_values.reserve(config->attributes.size());
        for (unsigned int i = 0; i < config->attributes.size(); ++i)
        {
            subtile.attribute_values.push_back(std::nullopt);
        }
        subtile.has_received_vector_data_attributes_once =
            !tileset->config->has_vector_data_layer_attributes;

        auto feature_table_json_data = tile_data.subspan(header_size, feature_table_json_size);
        auto feature_table_bin_data =
            tile_data.subspan(header_size + feature_table_json_size, feature_table_bin_size);
        if (!_decode_b3dm_feature_table(
                tile, &subtile, feature_table_json_data, feature_table_bin_data))
        {
            return false;
        }

        auto& content = std::get<ThreeDTile::B3dmContent>(subtile.content);

        {
            hrz::three_d_tiles::EncodedBatchTable params;
            params.batch_length = content.batch_length;
            params.json_data = hrz::blobs::make_sub_blob(
                hrz::unsafe("Component sizes are checked against tile size"), ba, tile_data_blob,
                header_size + feature_table_json_size + feature_table_bin_size,
                batch_table_json_size);
            params.bin_data = hrz::blobs::make_sub_blob(
                hrz::unsafe("Component sizes are checked against tile size"), ba, tile_data_blob,
                header_size + feature_table_json_size + feature_table_bin_size
                    + batch_table_json_size,
                batch_table_bin_size);
            params.attributes = config->attributes;
            content.decode_batch_table_ticket = hrz_jobs::add_job_decode_three_d_tiles_batch_table(
                js, params,
                {hrz::monitoring::systems::ThreeDTilesLayers, tileset->config->global_layer_id});
        }

        auto gltf_blob = hrz::blobs::make_sub_blob(
            hrz::unsafe("Component sizes are checked against tile size"), ba, tile_data_blob,
            gltf_offset);

        content.draw_prps = config->inherited_draw_prps;
        content.draw_prps.transform =
            tile.base_transform * subtile.rtc_transform * s_axes_transform;

        content.prototype = hrz::model::create_from_gltf_blob(
            ba, attributions, gltf_blob, tile.uri, tileset->base_url.derive_base(tile.uri),
            gltf_offset, tileset->config->headers, tileset->config->additional_attribution,
            get_request_queue(tileset->config),
            hrz::combine_loading_priorities(
                tileset->config->loading_priority, std::numeric_limits<uint16_t>::max()),
            {hrz::monitoring::systems::ThreeDTilesLayers, tileset->config->global_layer_id});

        content.materials.prototype_may_have_been_recreated(content.prototype);
        content.materials.recreate_all_materials(content.prototype, config->materials);
        content.materials.set_active_materials(
            config->base_material,
            content.draw_prps.overlay_material_enabled
                ? config->overlay_material
                : std::optional<std::string_view>(std::nullopt));

        // If the batch length is 0, all the geometry takes on the same style.
        size_t color_count = std::max((uint32_t)1, content.batch_length);
        // Fill the colours with pure white, so that the styling has no effect so far.
        content.per_batch_color.resize(color_count, lm::ubvec4{0xff});

        tile.subtiles.push_back(std::move(subtile));

        return true;
    }

    bool _load_i3dm(
        Tileset* tileset,
        ThreeDTile& tile,
        const hrz::blobs::BlobHandle& tile_data_blob,
        hrz::AssetsLoader* al,
        hrz::JobScheduler* js,
        hrz::BlobAllocator* ba,
        hrz::AttributionRegistry* attributions)
    {
        auto tile_data = tile_data_blob.get_data();
        const size_t tile_data_size = tile_data.size();

        auto magic = tile_data.subspan(0, 4);

        if (std::strncmp((const char*)magic.data(), "i3dm", 4) != 0) return false;

        const auto header_size = 32;

        if (tile_data_size < header_size)
        {
            HRZ_LOG_ERROR(
                "Not enough data received for i3dm tile \"{}\": {}", tile.uri, tile_data_size);
            return false;
        }

        auto header_data = tile_data.subspan(0, 32);

        auto format_version = read_uint32(header_data, 1);
        if (format_version != 1)
        {
            HRZ_LOG_ERROR("Unsupported i3dm version in tile \"{}\": {}", tile.uri, format_version);
            return false;
        }

        auto declared_size = read_uint32(header_data, 2);
        if (declared_size != tile_data_size)
        {
            HRZ_LOG_ERROR(
                "Wrong i3dm size in tile \"{}\": Expected {}, received {}", tile.uri, declared_size,
                tile_data_size);
            return false;
        }

        auto feature_table_json_size = read_uint32(header_data, 3);
        auto feature_table_bin_size = read_uint32(header_data, 4);
        auto batch_table_json_size = read_uint32(header_data, 5);
        auto batch_table_bin_size = read_uint32(header_data, 6);
        auto gltf_format = read_uint32(header_data, 7);

        auto gltf_offset = header_size + feature_table_json_size + feature_table_bin_size
            + batch_table_json_size + batch_table_bin_size;

        if (feature_table_json_size + feature_table_bin_size > tile_data_size)
        {
            HRZ_LOG_ERROR("Invalid i3dm header in tile \"{}\"", tile.uri);
            return false;
        }

        auto config = tileset->config;

        ThreeDTile::Subtile subtile;
        subtile.content.emplace<ThreeDTile::I3dmContent>();
        auto& content = std::get<ThreeDTile::I3dmContent>(subtile.content);

        auto feature_table_json_data = tile_data.subspan(header_size, feature_table_json_size);
        auto feature_table_bin_data =
            tile_data.subspan(header_size + feature_table_json_size, feature_table_bin_size);
        if (!_decode_i3dm_feature_table(
                tile, &subtile, feature_table_json_data, feature_table_bin_data))
        {
            return false;
        }

        {
            hrz::three_d_tiles::EncodedBatchTable params;
            params.batch_length = content.batch_length;
            params.json_data = hrz::blobs::make_sub_blob(
                hrz::unsafe("Component sizes are checked against tile size"), ba, tile_data_blob,
                header_size + feature_table_json_size + feature_table_bin_size,
                batch_table_json_size);
            params.bin_data = hrz::blobs::make_sub_blob(
                hrz::unsafe("Component sizes are checked against tile size"), ba, tile_data_blob,
                header_size + feature_table_json_size + feature_table_bin_size
                    + batch_table_json_size,
                batch_table_bin_size);
            params.attributes = config->attributes;
            content.decode_batch_table_ticket = hrz_jobs::add_job_decode_three_d_tiles_batch_table(
                js, params,
                {hrz::monitoring::systems::ThreeDTilesLayers, tileset->config->global_layer_id});
        }

        content.draw_prps = config->inherited_draw_prps;
        content.draw_prps.transform = s_axes_transform;

        if (gltf_format == 1) // Embedded glTF
        {
            auto gltf_span = tile_data.subspan(gltf_offset, tile_data.size() - gltf_offset);
            auto gltf_size = hrz::model::fetch_glb_declared_size(gltf_span);

            if (gltf_size > gltf_span.size())
            {
                HRZ_LOG_ERROR("Invalid embedded glTF size in i3dm tile \"{}\"", tile.uri);
                return false;
            }

            auto gltf_blob = hrz::blobs::make_sub_blob(
                hrz::unsafe("Offset and size are checked above"), ba, tile_data_blob, gltf_offset,
                gltf_size);

            content.model_uri_hash = std::nullopt;
            content.prototype = hrz::model::create_from_gltf_blob(
                ba, attributions, gltf_blob, tile.uri, tileset->base_url.derive_base(tile.uri),
                gltf_offset, tileset->config->headers, tileset->config->additional_attribution,
                get_request_queue(tileset->config),
                hrz::combine_loading_priorities(
                    tileset->config->loading_priority, std::numeric_limits<uint16_t>::max()),
                {hrz::monitoring::systems::ThreeDTilesLayers, tileset->config->global_layer_id});
        }
        else if (gltf_format == 0) // External glTF
        {
            hrz::BaseUrl i3dm_base_url = tileset->base_url.derive_base(tile.uri);
            auto gltf_blob = hrz::blobs::make_sub_blob(
                hrz::unsafe("Component sizes are checked against tile size"), ba, tile_data_blob,
                gltf_offset);
            auto gltf_data = gltf_blob.get_data();
            std::string_view partial_uri =
                hrz::str::rtrim_s({(const char*)gltf_data.data(), gltf_data.size()}, '\x20');
            std::string full_uri = i3dm_base_url.derive(partial_uri);

            content.model_uri_hash = make_model_uri_hash(full_uri, tileset->config->headers);

            uint32_t request_priority = hrz::combine_loading_priorities(
                tileset->config->loading_priority, std::numeric_limits<uint16_t>::max());

            auto it = _model_uris_to_prototypes.find(content.model_uri_hash.value());
            if (it == std::end(_model_uris_to_prototypes))
            {
                ModelPrototypeRef proto_ref;
                proto_ref.status = ModelPrototypeRef::Status::NEW;

                proto_ref.uri = full_uri;
                proto_ref.headers = tileset->config->headers;
                proto_ref.base_url = i3dm_base_url;
                proto_ref.data_offset = 0;
                proto_ref.additional_attribution = tileset->config->additional_attribution;
                proto_ref.load_queue = get_request_queue(tileset->config);
                proto_ref.loading_priority = request_priority;
                proto_ref.resource_owner = {
                    hrz::monitoring::systems::ThreeDTilesLayers, tileset->config->global_layer_id};

                proto_ref.ref_count = 1;

                _model_uris_to_prototypes[content.model_uri_hash.value()] = std::move(proto_ref);
            }
            else
            {
                auto& proto_ref = it->second;

                if (request_priority > proto_ref.loading_priority)
                {
                    proto_ref.loading_priority = request_priority;
                    proto_ref.load_queue = get_request_queue(tileset->config);
                }

                proto_ref.ref_count += 1;
            }
        }
        else
        {
            HRZ_LOG_ERROR("Invalid glTF format specifier in i3dm tile \"{}\"", tile.uri);
            return false;
        }

        size_t color_count = std::max((uint32_t)1, content.instances_length);
        // Fill the colours with pure white, so that the styling has no effect so far.
        content.per_instance_color.resize(color_count, lm::ubvec4{0xff});

        tile.subtiles.push_back(std::move(subtile));

        return true;
    }

    bool _load_pnts(
        Tileset* tileset,
        ThreeDTile& tile,
        const hrz::blobs::BlobHandle& tile_data_blob,
        hrz::AssetsLoader* al,
        hrz::JobScheduler* js,
        hrz::BlobAllocator* ba,
        hrz::AttributionRegistry* attributions) const
    {
        auto tile_data = tile_data_blob.get_data();
        const size_t tile_data_size = tile_data.size();

        auto magic = tile_data.subspan(0, 4);

        if (std::strncmp((const char*)magic.data(), "pnts", 4) != 0) return false;

        const auto header_size = 28;
        if (tile_data_size < header_size)
        {
            HRZ_LOG_ERROR(
                "Not enough data received for pnts tile \"{}\": {}", tile.uri, tile_data_size);
            return false;
        }

        auto header_data = tile_data.subspan(0, 28);

        auto format_version = read_uint32(header_data, 1);
        if (format_version != 1)
        {
            HRZ_LOG_ERROR("Unsupported pnts version in tile \"{}\": {}", tile.uri, format_version);
            return false;
        }

        auto feature_table_json_size = read_uint32(header_data, 3);
        auto feature_table_bin_size = read_uint32(header_data, 4);
        auto batch_table_json_size = read_uint32(header_data, 5);
        auto batch_table_bin_size = read_uint32(header_data, 6);

        if (header_size + feature_table_json_size + feature_table_bin_size > tile_data_size)
        {
            HRZ_LOG_ERROR("Invalid pnts header in tile \"{}\"", tile.uri);
            return false;
        }

        auto config = tileset->config;

        hrz::monitoring::ResourceOwner owner{
            hrz::monitoring::systems::ThreeDTilesLayers, tileset->config->global_layer_id};

        ThreeDTile::Subtile subtile;

        subtile.content.emplace<ThreeDTile::PntsContent>();

        auto feature_table_json_data = tile_data.subspan(header_size, feature_table_json_size);
        auto feature_table_bin_blob = tile_data_blob.make_sub_blob(
            hrz::unsafe("Component sizes are checked against tile size"),
            header_size + feature_table_json_size, feature_table_bin_size);
        if (!_decode_pnts_feature_table(
                tile, &subtile, feature_table_json_data, feature_table_bin_blob, owner, ba))
        {
            return false;
        }

        auto& content = std::get<ThreeDTile::PntsContent>(subtile.content);

        {
            hrz::three_d_tiles::EncodedBatchTable params;
            params.batch_length = content.batch_length;
            params.json_data = hrz::blobs::make_sub_blob(
                hrz::unsafe("Component sizes are checked against tile size"), ba, tile_data_blob,
                header_size + feature_table_json_size + feature_table_bin_size,
                batch_table_json_size);
            params.bin_data = hrz::blobs::make_sub_blob(
                hrz::unsafe("Component sizes are checked against tile size"), ba, tile_data_blob,
                header_size + feature_table_json_size + feature_table_bin_size
                    + batch_table_json_size,
                batch_table_bin_size);
            params.attributes = config->attributes;
            content.decode_batch_table_ticket =
                hrz_jobs::add_job_decode_three_d_tiles_batch_table(js, params, owner);
        }

        // If the batch length is 0, all the geometry takes on the same style.
        size_t color_count = std::max((uint32_t)1, content.batch_length);
        content.per_batch_color.resize(color_count, lm::ubvec4{0xff});

        content.update_appearance(config->inherited_draw_prps);

        tile.subtiles.push_back(std::move(subtile));

        return true;
    }

    bool _load_external_tileset(
        Tileset* tileset,
        uint32_t tile_index,
        ThreeDTile& tile,
        const hrz::blobs::BlobHandle& tile_data_blob,
        hrz::JobScheduler* js)
    {
        auto handle = _tilesets.alloc();
        auto external_tileset = _tilesets.get_object(handle);

        external_tileset->config = tileset->config;
        external_tileset->is_external = true;
        external_tileset->root_depth = tile.depth;

        external_tileset->transform = tile.base_transform;

        // This value usually determines when the root tile is displayed.
        // However, when a tileset is an external tileset, the decision to
        // display its root tile is made by its parent tileset.
        // Its geometric error is set to what will in practice decides its
        // visibility. This value is useful for the handling of empty tiles.
        external_tileset->geometric_error =
            _get_lower_geometric_error_in_parent_tiles(tileset, tile_index);

        external_tileset->base_url = tileset->base_url.derive_base(tile.uri);

        external_tileset->vector_data_attribute_request_id = handle;

        _decode_tileset_descriptor(external_tileset, tile_data_blob, js);

        external_tileset->requests_count_metric = hrz::metrics::MetricDesc(
            "3D Tiles (requests tally)", false,
            {{"layer", std::to_string(external_tileset->config->layer_handle)},
             {"url", external_tileset->base_url.base().c_str()}});
        hrz::metrics::increment_counter(&tileset->requests_count_metric);

        _loading_tilesets.insert(handle);

        ThreeDTile::Subtile subtile;
        ThreeDTile::TilesetContent content;
        content.tileset_handle = handle;
        subtile.content = content;

        tile.subtiles.push_back(std::move(subtile));
        assert(
            tile.subtiles.size()
            == 1); // We should only have 1 subtile if this tile is an external dataset

        return true;
    }

    bool _load_gltf_if_allowed(
        Tileset* tileset,
        ThreeDTile& tile,
        const hrz::blobs::BlobHandle& tile_data_blob,
        hrz::JobScheduler* js,
        hrz::BlobAllocator* ba,
        hrz::AttributionRegistry* attributions)
    {
        if (!tileset->allow_gltf_content)
        {
            return false;
        }

        auto config = tileset->config;

        ThreeDTile::Subtile subtile;
        subtile.content.emplace<ThreeDTile::B3dmContent>();

        subtile.attribute_values.reserve(config->attributes.size());
        for (unsigned int i = 0; i < config->attributes.size(); ++i)
        {
            subtile.attribute_values.push_back(std::nullopt);
        }
        subtile.has_received_vector_data_attributes_once =
            !tileset->config->has_vector_data_layer_attributes;

        auto tile_data = tile_data_blob.get_data();
        auto feature_table_json_data = tile_data.subspan(0, 0);
        auto feature_table_bin_data = tile_data.subspan(0, 0);
        _decode_b3dm_feature_table(tile, &subtile, feature_table_json_data, feature_table_bin_data);

        auto& content = std::get<ThreeDTile::B3dmContent>(subtile.content);

        {
            hrz::three_d_tiles::EncodedBatchTable params;
            params.batch_length = content.batch_length;
            params.json_data = {};
            params.bin_data = {};
            params.attributes = config->attributes;
            content.decode_batch_table_ticket = hrz_jobs::add_job_decode_three_d_tiles_batch_table(
                js, params,
                {hrz::monitoring::systems::ThreeDTilesLayers, tileset->config->global_layer_id});
        }

        auto& gltf_blob = tile_data_blob;

        content.draw_prps = config->inherited_draw_prps;
        content.draw_prps.transform =
            tile.base_transform * subtile.rtc_transform * s_axes_transform;

        content.prototype = hrz::model::create_from_gltf_blob(
            ba, attributions, gltf_blob, tile.uri, tileset->base_url.derive_base(tile.uri), 0,
            tileset->config->headers, tileset->config->additional_attribution,
            get_request_queue(tileset->config),
            hrz::combine_loading_priorities(
                tileset->config->loading_priority, std::numeric_limits<uint16_t>::max()),
            {hrz::monitoring::systems::ThreeDTilesLayers, tileset->config->global_layer_id});

        content.materials.prototype_may_have_been_recreated(content.prototype);
        content.materials.recreate_all_materials(content.prototype, config->materials);
        content.materials.set_active_materials(
            config->base_material,
            content.draw_prps.overlay_material_enabled
                ? config->overlay_material
                : std::optional<std::string_view>(std::nullopt));

        // If the batch length is 0, all the geometry takes on the same style.
        size_t color_count = std::max((uint32_t)1, content.batch_length);
        // Fill the colours with pure white, so that the styling has no effect so far.
        content.per_batch_color.resize(color_count, lm::ubvec4{0xff});

        tile.subtiles.push_back(std::move(subtile));

        return true;
    }

    bool _load_cmpt(
        Tileset* tileset,
        ThreeDTile& tile,
        const hrz::blobs::BlobHandle& tile_data_blob,
        hrz::JobScheduler* js,
        hrz::AssetsLoader* al,
        hrz::BlobAllocator* ba,
        hrz::AttributionRegistry* attributions)
    {
        auto tile_data = tile_data_blob.get_data();
        const size_t tile_data_size = tile_data.size();

        auto magic = tile_data.subspan(0, 4);

        if (std::strncmp((const char*)magic.data(), "cmpt", 4) != 0) return false;

        const auto header_size = 16;

        if (tile_data_size < header_size)
        {
            HRZ_LOG_ERROR(
                "Not enough data received for cmpt tile \"{}\": {}", tile.uri, tile_data_size);
            return false;
        }

        auto header_data = tile_data.subspan(0, header_size);

        auto format_version = read_uint32(header_data, 1);
        if (format_version != 1)
        {
            HRZ_LOG_ERROR("Unsupported cmpt version in tile \"{}\": {}", tile.uri, format_version);
            return false;
        }

        auto declared_size = read_uint32(header_data, 2);
        if (declared_size != tile_data_size)
        {
            HRZ_LOG_ERROR(
                "Wrong cmpt size in tile \"{}\": Expected {}, received {}", tile.uri, declared_size,
                tile_data_size);
            return false;
        }

        auto subtile_count = read_uint32(header_data, 3);

        if (subtile_count > 0)
        {
            size_t current_subtile_offset = header_size;
            const size_t subtile_header_size = 12;

            for (uint32_t i = 0; i < subtile_count; ++i)
            {
                auto subtile_data = tile_data.subspan(current_subtile_offset);
                if (subtile_data.size_bytes() < subtile_header_size)
                {
                    HRZ_LOG_ERROR(
                        "Not enough header data for subtile {} of composite tile \"{}\"", i,
                        tile.uri);
                    break;
                }

                uint32_t subtile_size = read_uint32(subtile_data, 2);
                if (subtile_data.size_bytes() < subtile_size)
                {
                    HRZ_LOG_ERROR(
                        "Not enough total data for subtile {} of composite tile \"{}\"", i,
                        tile.uri);
                    break;
                }

                auto subtile_data_blob = hrz::blobs::make_sub_blob(
                    hrz::unsafe("Offset and size are checked above"), ba, tile_data_blob,
                    current_subtile_offset, subtile_size);

                // Composite tiles can only contain other 3D Tiles formats, no raw glTF or external
                // tilesets.
                if (!_load_3dtiles_content(
                        tileset, tile, subtile_data_blob, js, al, ba, attributions))
                {
                    tile.has_unsupported_subtiles = true;
                }

                current_subtile_offset += subtile_size;
            }
        }

        return true;
    }

    bool _load_3dtiles_content(
        Tileset* tileset,
        ThreeDTile& tile,
        const hrz::blobs::BlobHandle& tile_data_blob,
        hrz::JobScheduler* js,
        hrz::AssetsLoader* al,
        hrz::BlobAllocator* ba,
        hrz::AttributionRegistry* attributions)
    {
        if (_load_b3dm(tileset, tile, tile_data_blob, js, ba, attributions))
        {
            return true;
        }
        else if (_load_i3dm(tileset, tile, tile_data_blob, al, js, ba, attributions))
        {
            return true;
        }
        else if (_load_pnts(tileset, tile, tile_data_blob, al, js, ba, attributions))
        {
            return true;
        }
        else
        {
            return _load_cmpt(tileset, tile, tile_data_blob, js, al, ba, attributions);
        }
    }

    bool _load_content(
        Tileset* tileset,
        ThreeDTile& tile,
        const hrz::blobs::BlobHandle& tile_data_blob,
        hrz::JobScheduler* js,
        hrz::AssetsLoader* al,
        hrz::BlobAllocator* ba,
        hrz::AttributionRegistry* attributions)
    {
        if (_load_gltf_if_allowed(tileset, tile, tile_data_blob, js, ba, attributions))
        {
            return true;
        }
        else
        {
            return _load_3dtiles_content(tileset, tile, tile_data_blob, js, al, ba, attributions);
        }
    }

    bool _decode_tile_data(
        uint32_t tile_index,
        ThreeDTile& tile,
        const hrz::blobs::BlobHandle& tile_data_blob,
        std::string_view tile_mime_type,
        Tileset* tileset,
        hrz::AssetsLoader* al,
        hrz::JobScheduler* js,
        hrz::BlobAllocator* ba,
        hrz::AttributionRegistry* attributions)
    {
        HRZ_SCOPED_SAMPLE("decode tile data");

        auto tile_data_size = tile_data_blob.data_size();
        if (tile_data_size < 4)
        {
            HRZ_LOG_ERROR("Not enough data in tile \"{}\"", tile.uri);
            return false;
        }

        auto tile_data = tile_data_blob.get_data();

        if (tile_mime_type == "application/json")
        {
            if (_load_external_tileset(tileset, tile_index, tile, tile_data_blob, js)) return true;
        }
        else if (tile_mime_type == "model/gltf-binary" || tile_mime_type == "model/gltf+json")
        {
            if (_load_gltf_if_allowed(tileset, tile, tile_data_blob, js, ba, attributions))
                return true;
        }
        else if (tile_mime_type == "application/octet-stream")
        {
            if (_load_content(tileset, tile, tile_data_blob, js, al, ba, attributions)) return true;
        }

        if (tile.uri.ends_with(".json"))
        {
            // This is probably an external tileset, sent with the wrong content-type header.
            if (_load_external_tileset(tileset, tile_index, tile, tile_data_blob, js)) return true;
        }
        else if (tile.uri.ends_with(".b3dm"))
        {
            // This is probably a b3dm, sent with the wrong content-type header.
            if (_load_b3dm(tileset, tile, tile_data_blob, js, ba, attributions)) return true;
        }
        else if (tile.uri.ends_with(".i3dm"))
        {
            // This is probably an i3dm, sent with the wrong content-type header.
            if (_load_i3dm(tileset, tile, tile_data_blob, al, js, ba, attributions)) return true;
        }
        else if (tile.uri.ends_with(".pnts"))
        {
            // This is probably a pnts, sent with the wrong content-type header.
            if (_load_pnts(tileset, tile, tile_data_blob, al, js, ba, attributions)) return true;
        }
        else if (tile.uri.ends_with(".cmpt"))
        {
            // This is probably a cmpt, sent with the wrong content-type header.
            if (_load_cmpt(tileset, tile, tile_data_blob, js, al, ba, attributions)) return true;
        }
        else if (tile.uri.ends_with(".glb") || tile.uri.ends_with(".gltf"))
        {
            // This is probably a gltf, sent with the wrong content-type header.
            if (_load_gltf_if_allowed(tileset, tile, tile_data_blob, js, ba, attributions))
                return true;
        }

        HRZ_LOG_ERROR("Unsupported format for tile \"{}\"", tile.uri);
        tile.has_unsupported_subtiles = true;
        return false;
    }

    /**
     * Compute the screen-space used to determine whether the root tile
     * should be rendered or not.
     */
    static hrz::StaticVector<double, hrz::SCENE_VIEW_COUNT> _compute_root_screen_space_errors(
        const ThreeDTile& root_tile,
        double root_geometric_error,
        std::span<const hrz::RenderViewInfo> views_info,
        std::span<const hrz::render::ScreenSpaceError> sses,
        double max_screen_space_error)
    {
        HRZ_SCOPED_SAMPLE_A("compute root screen space error");

        hrz::StaticVector<double, hrz::SCENE_VIEW_COUNT> errors;
        assert(views_info.size() == sses.size());

        for (size_t i = 0; i < views_info.size(); ++i)
        {
            double distance = hrz::three_d_tiles::distance(
                root_tile.bounding_volume, views_info[i].cam_view_info.cam.pos);
            double error = sses[i].compute_screen_space_error(
                root_geometric_error, distance, max_screen_space_error);

            errors.push_back(error);
        }

        return errors;
    }

    /**
     * Compute the screen-space used to determine whether a tile should
     * be refined or not.
     */
    hrz::StaticVector<double, hrz::SCENE_VIEW_COUNT> _compute_tile_screen_space_errors(
        const ThreeDTile& tile,
        std::span<const hrz::RenderViewInfo> views_info,
        std::span<const hrz::render::ScreenSpaceError> sses,
        double max_screen_space_error)
    {
        HRZ_SCOPED_SAMPLE_A("compute tile screen space error");

        hrz::StaticVector<double, hrz::SCENE_VIEW_COUNT> errors;

        // If the tile cannot be refined, don't bother computing its screen-space error.
        if (tile.geometric_error == 0)
        {
            for (size_t i = 0; i < views_info.size(); ++i)
            {
                errors.push_back(0);
            }
        }
        else
        {
            assert(views_info.size() == sses.size());

            for (size_t i = 0; i < views_info.size(); ++i)
            {
                double distance = hrz::three_d_tiles::distance(
                    tile.bounding_volume, views_info[i].cam_view_info.cam.pos);
                double error = sses[i].compute_screen_space_error(
                    tile.geometric_error, distance, max_screen_space_error);

                errors.push_back(error);
            }
        }

        return errors;
    }

    void _decode_tileset_descriptor(
        Tileset* tileset,
        const hrz::blobs::BlobHandle& descriptor_blob,
        hrz::JobScheduler* js)
    {
        hrz::three_d_tiles::EncodedThreeDTilesTileset params;
        params.raw_json = descriptor_blob;
        params.transform = tileset->transform;
        params.root_depth = tileset->root_depth;
        tileset->decode_descriptor_ticket = hrz_jobs::add_job_decode_three_d_tiles_tileset(
            js, params,
            {hrz::monitoring::systems::ThreeDTilesLayers, tileset->config->global_layer_id});

        tileset->status = Tileset::Status::DECODING_DESCRIPTOR;
    }

    bool _can_be_styled(const ThreeDTile::Subtile& subtile)
    {
        return subtile.has_b3dm() || subtile.has_i3dm() || subtile.has_pnts();
    }

    bool _is_styled(const ThreeDTile::Subtile& subtile)
    {
        return std::visit(
            hrz::overload{
                [](const ThreeDTile::B3dmContent& content) { return content.has_been_styled_once; },
                [](const ThreeDTile::I3dmContent& content) { return content.has_been_styled_once; },
                [](const ThreeDTile::PntsContent& content) { return content.has_been_styled_once; },
                [](const ThreeDTile::TilesetContent&) { return true; },
                [](const std::monostate&) { return true; }},
            subtile.content);
    }

    hrz::RenderRequest _update_visibility_constraints(
        TilesetConfig* config,
        std::span<const hrz::RenderViewInfo> views_info)
    {
        hrz::RenderRequest render_request;

        auto new_result = hrz::layers::are_visibility_constraints_satisfied(
            views_info, config->visibility_constraints);

        if (new_result != config->visibility_constraints_result)
        {
            render_request.request_visual_render();
        }

        config->visibility_constraints_result = new_result;

        return render_request;
    }

    void _work_loading_tilesets(
        hrz::AssetsLoader* al,
        hrz::JobScheduler* js,
        hrz::BlobAllocator* ba,
        std::span<const hrz::RenderViewInfo> views_info)
    {
        HRZ_SCOPED_SAMPLE("work loading tilesets");

        for (auto tileset_handle : _loading_tilesets)
        {
            auto tileset = _tilesets.get_object(tileset_handle);
            _update_visibility_constraints(tileset->config, views_info);
        }

        for (auto it = _loading_tilesets.begin(); it != _loading_tilesets.end();)
        {
            TilesetH handle = *it;
            auto tileset = _tilesets.get_object(handle);

            if (!tileset->config->is_visible
                || !tileset->config->visibility_constraints_result.satisfied_in)
            {
                ++it;
                continue;
            }

            bool erase = false;
            if (tileset->status == Tileset::Status::LOADING_DESCRIPTOR)
            {
                if (hrz::assets_loader::is_finished(al, tileset->load_descriptor_ticket))
                {
                    if (hrz::assets_loader::get_status(al, tileset->load_descriptor_ticket)
                        == hrz::assets_loader::RequestStatus::Loaded)
                    {
                        auto descriptor_blob =
                            hrz::assets_loader::get_blob(al, ba, tileset->load_descriptor_ticket);
                        _decode_tileset_descriptor(tileset, std::move(descriptor_blob), js);
                    }
                    else
                    {
                        HRZ_LOG_ERROR(
                            "Error: Could not load 3D Tiles descriptor at \"{}\"", tileset->url);

                        tileset->status = Tileset::Status::ERROR;

                        erase = true;
                    }

                    hrz::assets_loader::end(al, tileset->load_descriptor_ticket);
                }
            }
            else if (tileset->status == Tileset::Status::DECODING_DESCRIPTOR)
            {
                if (hrz_jobs::is_job_finished(js, tileset->decode_descriptor_ticket))
                {
                    if (hrz_jobs::get_job_status(js, tileset->decode_descriptor_ticket)
                        == hrz::job_scheduler::JobStatus::Finished_Success)
                    {
                        hrz::three_d_tiles::ThreeDTilesTilesetDescriptor response;
                        hrz_jobs::get_job_response(js, tileset->decode_descriptor_ticket, response);

                        for (size_t i = 0; i < response.tiles.size(); ++i)
                        {
                            auto& response_tile = response.tiles.at(i);

                            tileset->tiles.emplace_back();
                            auto& tile = tileset->tiles.back();

                            tile.depth = response_tile.depth;
                            tile.index = i;
                            tile.parent_index = response_tile.parent_index;
                            tile.child_count = response_tile.child_count;
                            tile.first_child_link_index = response_tile.first_child_link_index;
                            tile.refinement_type = response_tile.refinement_type;

                            tile.geometric_error = response_tile.geometric_error;

                            tile.uri = std::move(response_tile.uri);
                            tile.range = response_tile.range;

                            tile.load_status = ThreeDTile::LoadStatus::UNLOADED;
                            tile.render_children_in.reset();
                            tile.render_self_in.reset();
                            tile.culled_in.reset();
                            tile.active_child_count = 0;
                            tile.renderable_child_count = 0;
                            std::ranges::fill(tile.rendered_child_count_per_view, 0);
                            tile.should_render_children_in.reset();
                            tile.delete_if_unused = true;

                            tile.base_transform = response_tile.transform;
                            tile.normal_transform =
                                lm::mat3(hrz::compute_normal_transform_matrix(tile.base_transform));

                            tile.bounding_volume = response_tile.bounding_volume;
                            if (response_tile.viewer_bounding_volume.has_value())
                            {
                                tile.viewer_bounding_volume =
                                    response_tile.viewer_bounding_volume.value();
                            }
                            if (response_tile.content_bounding_volume.has_value())
                            {
                                tile.content_bounding_volume =
                                    response_tile.content_bounding_volume.value();
                                tile.horizon_occlusion_point =
                                    hrz::three_d_tiles::compute_horizon_occlusion_point(
                                        tile.content_bounding_volume.value());
                            }
                            else
                            {
                                tile.horizon_occlusion_point =
                                    hrz::three_d_tiles::compute_horizon_occlusion_point(
                                        tile.bounding_volume);
                            }

                            tile.has_unsupported_subtiles = false;
                            tile.subtiles_left_to_load = 0;

#if DRAW_DEBUG_BOXES
                            tile.box.vertices = make_box(tile.bounding_volume);

                            lm::ubvec4 color = hrz::convert_uint_color_to_bytes(
                                (uint32_t)hrz::three_d_tiles::compute_hash(tile.bounding_volume));
                            color.a = 255;
                            tile.box.colors =
                                std::vector<lm::ubvec4>(tile.box.vertices.size() / 2, color);
#endif
                        }

                        tileset->root_tile_index = response.root_tile_index;
                        tileset->child_links = std::move(response.child_links);
                        tileset->allow_gltf_content = response.allow_gltf_content;

                        // If the tileset is external, use the value given by its parent tileset.
                        if (!tileset->is_external)
                        {
                            tileset->geometric_error = response.geometric_error;
                        }

                        if (response.tiles.size() > 0)
                        {
                            tileset->render = !tileset->is_external;

                            tileset->status = Tileset::Status::LOADED;

                            _loaded_tilesets.insert(handle);
                        }
                        else
                        {
                            HRZ_LOG_ERROR("No tiles in tileset descriptor at \"{}\"", tileset->url);

                            tileset->status = Tileset::Status::ERROR;
                        }
                    }
                    else
                    {
                        hrz_jobs::cancel_job(js, tileset->decode_descriptor_ticket);

                        HRZ_LOG_ERROR(
                            "Error while decoding 3D Tiles tileset descriptor at \"{}\"",
                            tileset->url);

                        tileset->status = Tileset::Status::ERROR;
                    }

                    erase = true;
                }
            }
            else
            {
                assert(false && "Invalid state");
            }

            if (erase)
            {
                _loading_tilesets.erase(it++);
            }
            else
            {
                ++it;
            }
        }
    }

    static hrz::StaticVector<std::array<lm::dvec4, 4>, hrz::SCENE_VIEW_COUNT> _compute_views_planes(
        std::span<const hrz::RenderViewInfo> views_info)
    {
        // Compute planes for the view.
        // See https://fgiesen.wordpress.com/2012/08/31/frustum-planes-from-the-projection-matrix/

        // Do not compute the Z view planes, as we don't want to cull using
        // the near and far planes. (They change depending on what is
        // displayed, and here we can load tiles that will affect them.)

        hrz::StaticVector<std::array<lm::dvec4, 4>, hrz::SCENE_VIEW_COUNT> planes;

        for (const auto& view_info : views_info)
        {
            lm::dmat4 pv_t = lm::transpose(view_info.cam_view_info.pv);

            auto normalize = [](const lm::dvec4& plane)
            {
                double inv_length = 1.0 / lm::length(plane.xyz);
                lm::dvec4 p = plane;
                p *= inv_length;
                return p;
            };

            planes.push_back(
                {normalize(-pv_t.w - pv_t.x), normalize(-pv_t.w + pv_t.x),
                 normalize(-pv_t.w - pv_t.y), normalize(-pv_t.w + pv_t.y)});
        }

        return planes;
    }

    static hrz::StaticVector<std::array<lm::dvec3, 5>, hrz::SCENE_VIEW_COUNT>
    _compute_views_vertices(std::span<const hrz::RenderViewInfo> views_info)
    {
        hrz::StaticVector<std::array<lm::dvec3, 5>, hrz::SCENE_VIEW_COUNT> vertices;

        // Reduce the near plane to a single position.

        for (const auto& view_info : views_info)
        {
            auto clip_to_world = [&](double x, double y, double z)
            {
                auto p = (view_info.cam_view_info.inv_pv * lm::dvec4(lm::dvec3(x, y, z), 1.0));
                p /= p.w;
                return p.xyz;
            };

            vertices.push_back({
                clip_to_world(-1.0, -1.0, 1.0),
                clip_to_world(1.0, -1.0, 1.0),
                clip_to_world(1.0, 1.0, 1.0),
                clip_to_world(1.0, 1.0, 1.0),
                view_info.cam_view_info.cam.pos,
            });
        }

        return vertices;
    }

    static hrz::SceneViewBitset compute_space_subset_intersections(
        std::span<const hrz::RenderViewInfo> views_info,
        const BoundingVolume& volume,
        std::span<const std::array<lm::dvec4, 4>> views_planes,
        std::span<const std::array<lm::dvec3, 5>> views_vertices)
    {
        hrz::SceneViewBitset bitset;

        HRZ_SCOPED_SAMPLE_A("compute space subset intersections");

        for (size_t i = 0; i < views_planes.size(); ++i)
        {
            if (hrz::three_d_tiles::intersects_space_subset(
                    volume, views_planes[i], views_vertices[i]))
            {
                bitset.set((int)views_info[i].view);
            }
        }

        return bitset;
    }

    static TilesetConfig::ScreenSpaceErrorHysteresis compute_screen_space_error_hysteresis(
        double refinement_hysteresis)
    {
        refinement_hysteresis = std::max(0.0, refinement_hysteresis);
        return {std::exp(-refinement_hysteresis), std::exp(refinement_hysteresis)};
    }

    static void threshold_screen_space_error(
        std::span<const hrz::RenderViewInfo> views_info,
        std::span<const double> sses,
        hrz::SceneViewBitset* below_min_screen_space_error_in,
        hrz::SceneViewBitset* above_max_screen_space_error_in,
        TilesetConfig::ScreenSpaceErrorHysteresis screen_space_error_hysteresis)
    {
        assert(screen_space_error_hysteresis.min_error <= screen_space_error_hysteresis.max_error);

        below_min_screen_space_error_in->reset();
        above_max_screen_space_error_in->reset();

        for (size_t i = 0; i < sses.size(); ++i)
        {
            if (sses[i] < screen_space_error_hysteresis.min_error)
            {
                below_min_screen_space_error_in->set((int)views_info[i].view);
            }
            else if (sses[i] >= screen_space_error_hysteresis.max_error)
            {
                above_max_screen_space_error_in->set((int)views_info[i].view);
            }
        }
    }

    static void test_horizon_occlusion(
        std::span<const hrz::RenderViewInfo> views_info,
        std::span<const hrz::HorizonCuller> cullers,
        const std::optional<lm::dvec3>& horizon_occlusion_point,
        hrz::SceneViewBitset* occluded_by_horizon_in)
    {
        occluded_by_horizon_in->reset();

        if (!horizon_occlusion_point.has_value())
        {
            return;
        }

        for (size_t i = 0; i < cullers.size(); ++i)
        {
            if (cullers[i].is_occluded(horizon_occlusion_point.value()))
            {
                occluded_by_horizon_in->set(views_info[i].view);
            }
        }
    }

    static hrz::RenderRequest _reset_subtile_style(ThreeDTile::Subtile& subtile)
    {
        if (subtile.has_b3dm())
        {
            auto& content = std::get<ThreeDTile::B3dmContent>(subtile.content);

            std::ranges::fill(content.per_batch_color, lm::ubvec4{0xff});

            return hrz::RenderRequest::visual();
        }
        else if (subtile.has_i3dm())
        {
            auto& content = std::get<ThreeDTile::I3dmContent>(subtile.content);

            std::ranges::fill(content.per_instance_color, lm::ubvec4{0xff});

            return hrz::RenderRequest::visual();
        }
        else if (subtile.has_pnts())
        {
            auto& content = std::get<ThreeDTile::PntsContent>(subtile.content);
            std::ranges::fill(content.per_batch_color, lm::ubvec4{0xff});
            content.has_transparent_feature = false;
            return hrz::RenderRequest::visual();
        }
        else
        {
            return {};
        }
    }

    static bool _needs_attribute_values(const TilesetConfig& config, ThreeDTile::Subtile& subtile)
    {
        return config.has_vector_data_layer_attributes && subtile.batches_to_feature_ids.size() > 0;
    }

    ThreeDTile::StylingStatus _request_attribute_values(
        const TilesetConfig& config,
        ThreeDTile::Subtile& subtile)
    {
        switch (subtile.vector_data_attribute_status)
        {
            case ThreeDTile::Subtile::VectorDataAttributeStatus::UNREQUESTED:
            {
                switch (config.vector_data_layer_status)
                {
                    case TilesetConfig::VectorDataLayerStatus::UNREQUESTED:
                    case TilesetConfig::VectorDataLayerStatus::LOADING:
                    {
                        return ThreeDTile::StylingStatus::WAITING_FOR_VECTOR_DATA_LAYER;
                    }
                    case TilesetConfig::VectorDataLayerStatus::LOADED:
                    {
                        _vector_data_channel.send(hrz::vector_data::messages::RequestData{
                            subtile.vector_data_attribute_request_id,
                            config.vector_data_layer_request_id, subtile.batches_to_feature_ids,
                            hrz::vector_data::DataKind::AttributeValues});
                        subtile.vector_data_attribute_status =
                            ThreeDTile::Subtile::VectorDataAttributeStatus::LOADING;
                        return ThreeDTile::StylingStatus::WAITING_FOR_ATTRIBUTE_VALUES;
                    }
                    default:
                    {
                        assert(false && "Unhandled case");
                        break;
                    }
                }
                break;
            }
            case ThreeDTile::Subtile::VectorDataAttributeStatus::LOADING:
            {
                return ThreeDTile::StylingStatus::WAITING_FOR_ATTRIBUTE_VALUES;
            }
            case ThreeDTile::Subtile::VectorDataAttributeStatus::LOADED:
            {
                assert(false);
                break;
            }
            case ThreeDTile::Subtile::VectorDataAttributeStatus::ERROR:
            {
                break;
            }
            default:
            {
                assert(false && "Unhandled case");
                break;
            }
        }

        return ThreeDTile::StylingStatus::IDLE;
    }

    // Return true if the job has been started.
    bool _try_start_style_job(
        const TilesetConfig& config,
        const ThreeDTile& tile,
        ThreeDTile::Subtile& subtile,
        hrz::JobScheduler* js)
    {
        assert(!hrz_jobs::is_job_valid(js, subtile.style_job_ticket));

        if (subtile.attribute_values.size() != config.attributes.size()) return false;

        hrz::style::FeaturesStylingData job_data;

        job_data.ast = config.style_ast;
        job_data.representations.push_back({TILE_REPR_ID, TILE_REPR_NAME});
        job_data.attributes.reserve(config.attributes.size());

        // Tiles without a batch table are stylable, but they have
        // no feature ids and attribute values.
        // In order to execute the styling script and have a computed
        // style for the tile, we pretend that there is one feature,
        // encompassing the whole tile.
        // Dummy attribute values have been generated in the batch
        // table decoding job.
        uint32_t batch_length = subtile.batch_length();
        job_data.feature_count = batch_length == 0 ? 1 : batch_length;

        for (unsigned int i = 0; i < config.attributes.size(); ++i)
        {
            if (!subtile.attribute_values.at(i).has_value()) return false;

            const auto& attribute = config.attributes.at(i);
            const auto& attribute_values = subtile.attribute_values.at(i).value();

            job_data.attributes.emplace(attribute.style_id, attribute_values);
            if (!attribute_values.out_of_line_data.empty())
            {
                job_data.attributes[attribute.style_id].out_of_line_data =
                    attribute_values.out_of_line_data;
            }
        }

        bool has_feature_ids = subtile.batches_to_feature_ids.has_any_attribute();
        if (!has_feature_ids)
        {
            job_data.rng_seed = hrz::hash_values(tile.uri, tile.range, config.rng_seed);
        }
        else
        {
            job_data.rng_seed = config.rng_seed;
            job_data.feature_ids_hashes = subtile.batches_to_feature_ids.hashes();
        }

        job_data.uniforms.emplace(
            _tile_depth_uniform_id,
            hrz::vector_data::attr_from<hrz::vector_data::OwnedAttributeValue>(tile.depth));

        for (const auto& palette : config.style_palettes)
        {
            job_data.palettes.push_back(palette);
        }

        subtile.style_job_ticket = hrz_jobs::add_job_style_features(
            js, job_data, {hrz::monitoring::systems::ThreeDTilesLayers, config.global_layer_id});

        return true;
    }

    static hrz::RenderRequest _retrieve_style_data_from_job(
        ThreeDTile::Subtile& subtile,
        hrz::JobScheduler* js)
    {
        hrz::style::StylingResult result;
        hrz_jobs::get_job_response(js, subtile.style_job_ticket, result);

        // This scratch buffer can be used by to temporarily store per batch colors.
        std::vector<lm::ubvec4> scratch;

        std::span<lm::ubvec4> per_batch_color;
        std::span<lm::ubvec4> per_instance_color;
        std::span<const uint32_t> instance_to_batch;

        bool* has_transparency_ptr = nullptr;

        if (subtile.has_b3dm())
        {
            per_batch_color = std::get<ThreeDTile::B3dmContent>(subtile.content).per_batch_color;
        }
        else if (subtile.has_i3dm())
        {
            auto& content = std::get<ThreeDTile::I3dmContent>(subtile.content);

            per_instance_color = content.per_instance_color;
            instance_to_batch = content.batch_id_data;

            scratch.resize(std::max(content.batch_length, 1U));
            per_batch_color = scratch;
        }
        else if (subtile.has_pnts())
        {
            auto& content = std::get<ThreeDTile::PntsContent>(subtile.content);
            per_batch_color = content.per_batch_color;
            has_transparency_ptr = &content.has_transparent_feature;
        }
        else
        {
            assert(!"We shouldn't be here");
        }

        if (!per_batch_color.data()) return {};

        // If a feature has been emitted, it will get
        // its colour down below. So by initialising
        // all the colours to 0, features that have not
        // been emitted are made invisible.
        std::ranges::fill(per_batch_color, lm::ubvec4{0});

        auto result_prps = result.features.prps.get_data();
        auto result_values = result.features.get_values_reader();
        bool has_transparent_color = false;

        for (const auto& instance : result.features.instances.get_data())
        {
            if (instance.repr_id == TILE_REPR_ID)
            {
                auto batch_id = instance.feature_index;

                lm::ubvec4 color = {0xff, 0xff, 0xff, 0xff};

                for (uint64_t prp_index = instance.first_prp;
                     prp_index < instance.first_prp + instance.prp_count; ++prp_index)
                {
                    if (result_prps[prp_index] == COLOR_PRP)
                    {
                        color = result_values.as_color(prp_index);
                    }
                }

                if (color.a > 0 && color.a < 0xff)
                {
                    has_transparent_color = true;
                }

                per_batch_color[batch_id] = color;
            }
        }

        if (!per_instance_color.empty())
        {
            for (uint32_t inst = 0; inst < per_instance_color.size(); ++inst)
            {
                if (inst < instance_to_batch.size())
                {
                    const uint32_t batch_id = instance_to_batch[inst];
                    if (batch_id < per_batch_color.size())
                    {
                        per_instance_color[inst] = per_batch_color[batch_id];
                    }
                }
                else
                {
                    per_instance_color[inst] = per_batch_color[0];
                }
            }
        }

        if (has_transparency_ptr)
        {
            *has_transparency_ptr = has_transparent_color;
        }

        return hrz::RenderRequest::visual();
    }

    struct WorkContext
    {
        hrz::AssetsLoader* al;
        hrz::JobScheduler* js;
        hrz::BlobAllocator* ba;
        hrz::ImageDecoder* imgdec;
        hrz::AttributionRegistry* attributions;
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
        bool should_update_priorities;
    };

    static uint32_t _compute_request_priority(
        const ThreeDTile& tile,
        const TilesetConfig& config,
        std::span<const hrz::RenderViewInfo> views_info,
        hrz::SceneViewBitset visiting_in)
    {
        double distance_to_view = DBL_MAX;
        for (const auto& view_info : views_info)
        {
            if (!visiting_in.is_set((int)view_info.view)) continue;

            double dist = distance(tile.bounding_volume, view_info.cam_view_info.cam.pos);
            if (dist < distance_to_view)
            {
                distance_to_view = dist;
            }
        }

        return hrz::combine_loading_priorities(
            config.loading_priority,
            std::numeric_limits<uint16_t>::max()
                - hrz::clamp_cast<double, uint16_t>(distance_to_view));
    }

    static void _start_loading_tile_data(
        Tileset* tileset,
        ThreeDTile* tile,
        const TilesetWorkContext& ctx)
    {
        assert(
            tile->load_status == ThreeDTile::LoadStatus::UNLOADED
            || tile->load_status == ThreeDTile::LoadStatus::LOAD_WHEN_INSIDE_VIEWER_VOLUME);
        if (!tile->range.has_value())
        {
            tile->load_data_ticket = hrz::assets_loader::begin(
                ctx.al, tileset->base_url.derive(tile->uri), tileset->config->headers,
                get_request_queue(tileset->config),
                _compute_request_priority(*tile, *tileset->config, ctx.views_info, ctx.visiting_in),
                {hrz::monitoring::systems::ThreeDTilesLayers, tileset->config->global_layer_id});
        }
        else
        {
            tile->load_data_ticket = hrz::assets_loader::begin(
                ctx.al, tileset->base_url.derive(tile->uri), tile->range.value().offset,
                tile->range.value().length, tileset->config->headers,
                get_request_queue(tileset->config),
                _compute_request_priority(*tile, *tileset->config, ctx.views_info, ctx.visiting_in),
                {hrz::monitoring::systems::ThreeDTilesLayers, tileset->config->global_layer_id});
        }
        hrz::metrics::increment_counter(&tileset->requests_count_metric);
        tile->load_status = ThreeDTile::LoadStatus::LOADING_TILE;
    }

    static void _load_tile(
        Tileset* tileset,
        uint32_t tile_index,
        ThreeDTile& tile,
        const TilesetWorkContext& ctx)
    {
        if (tile.uri.size() > 0)
        {
            if (tile.viewer_bounding_volume.has_value())
            {
                tile.load_status = ThreeDTile::LoadStatus::LOAD_WHEN_INSIDE_VIEWER_VOLUME;

                if (tile_index != tileset->root_tile_index)
                {
                    auto& parent_tile = tileset->tiles.at(tile.parent_index);
                    parent_tile.renderable_child_count += 1;
                    assert(parent_tile.renderable_child_count <= parent_tile.child_count);
                }
            }
            else
            {
                _start_loading_tile_data(tileset, &tile, ctx);
            }
        }
        else
        {
            // The tile is empty.
            // The 3D Tiles specification says that the tile must be considered loaded.
            // If the parent tile has content, and the [grand-]children also have content,
            // it could result in a transient empty space on the planet.
            // However Cesium themselves use a different strategy and only consider an
            // empty tile as loaded if its children are displayed (recursively), so that
            // content is always shown.
            // Some 3D Tiles producers rely on that behaviour for smooth loading and
            // display of their tilesets.
            // Here empty tiles with children are not set to loaded, but to be loading
            // their content. Then the loading code sets them to loaded once the children
            // are loaded. This recursively forces empty tiles that are between non-empty
            // tiles to wait until content has been loaded, and prevents the apparition of
            // holes.
            // However some of the [grand-]children may not be close enough, or in the
            // camera frustum, and will not load. The loading code sets the empty tile to
            // the loaded state in that case, to avoid blocking a whole sub-tree.
            // See
            //     - https://github.com/CesiumGS/3d-tiles/issues/609
            //     - https://github.com/CesiumGS/cesium/issues/9356
            //     - https://github.com/CesiumGS/cesium-native/pull/488
            tile.load_status = tile.child_count > 0 ? ThreeDTile::LoadStatus::LOADING_CONTENT
                                                    : ThreeDTile::LoadStatus::LOADED;

            tile.is_empty_structural = tile.load_status == ThreeDTile::LoadStatus::LOADING_CONTENT
                && tile.geometric_error
                    > _get_lower_geometric_error_in_parent_tiles(tileset, tile_index);
        }

        tile.delete_if_unused = false;
        tileset->active_tiles.insert(tile_index);
    }

    // Start or restart the styling process.
    hrz::RenderRequest _start_styling_process(
        const TilesetConfig& config,
        ThreeDTile& tile,
        ThreeDTile::Subtile& subtile,
        bool reload_attribute_values,
        const TilesetWorkContext& ctx)
    {
        hrz::RenderRequest render_request;

        if (subtile.styling_status == ThreeDTile::StylingStatus::WAITING_FOR_JOB)
        {
            assert(hrz_jobs::is_job_valid(ctx.js, subtile.style_job_ticket));
            hrz_jobs::cancel_job(ctx.js, subtile.style_job_ticket);
        }

        if (!config.has_style)
        {
            render_request |= _reset_subtile_style(subtile);
            subtile.styling_status = ThreeDTile::StylingStatus::WAITING_FOR_RESOURCE_UPDATE;
        }
        else if (
            (reload_attribute_values || !subtile.has_received_vector_data_attributes_once
             || subtile.vector_data_attribute_status
                 == ThreeDTile::Subtile::VectorDataAttributeStatus::UNREQUESTED)
            && _needs_attribute_values(config, subtile))
        {
            if (reload_attribute_values
                && subtile.vector_data_attribute_status
                    != ThreeDTile::Subtile::VectorDataAttributeStatus::UNREQUESTED)
            {
                _vector_data_channel.send(hrz::vector_data::messages::ReleaseDataRequest{
                    subtile.vector_data_attribute_request_id});
                subtile.vector_data_attribute_status =
                    ThreeDTile::Subtile::VectorDataAttributeStatus::UNREQUESTED;
            }

            subtile.styling_status = _request_attribute_values(config, subtile);
        }
        else if (
            subtile.vector_data_attribute_status
                != ThreeDTile::Subtile::VectorDataAttributeStatus::ERROR
            && _try_start_style_job(config, tile, subtile, ctx.js))
        {
            subtile.styling_status = ThreeDTile::StylingStatus::WAITING_FOR_JOB;
        }
        else
        {
            // There was an error somewhere.
            render_request |= _reset_subtile_style(subtile);
            subtile.styling_status = ThreeDTile::StylingStatus::WAITING_FOR_RESOURCE_UPDATE;
        }

        return render_request;
    }

    static hrz::RenderRequest _work_loaded_root_tile(
        Tileset* tileset,
        const TilesetWorkContext& ctx)
    {
        hrz::RenderRequest render_request;
        hrz::SceneViewBitset root_tile_needed_in = false;
        const auto* config = tileset->config;

        auto& root_tile = tileset->tiles.at(tileset->root_tile_index);
        auto screen_space_errors = _compute_root_screen_space_errors(
            root_tile, tileset->geometric_error, ctx.views_info, ctx.sses,
            config->max_screen_space_error);

        hrz::SceneViewBitset above_max_screen_space_error_in;
        hrz::SceneViewBitset below_min_screen_space_error_in;
        threshold_screen_space_error(
            ctx.views_info, screen_space_errors, &below_min_screen_space_error_in,
            &above_max_screen_space_error_in, tileset->config->screen_space_error_hysteresis);

        hrz::SceneViewBitset occluded_by_horizon_in;
        test_horizon_occlusion(
            ctx.views_info, ctx.horizon_cullers, root_tile.horizon_occlusion_point,
            &occluded_by_horizon_in);

        hrz::SceneViewBitset intersects_space_subset_in;
        bool computed_space_subset_intersections = false;

        // Lazy initialization of intersects_space_subset_in
        auto get_intersects_space_subset_in = [&]() -> const hrz::SceneViewBitset&
        {
            if (!computed_space_subset_intersections)
            {
                intersects_space_subset_in = ctx.visiting_in
                    & compute_space_subset_intersections(
                                                 ctx.views_info, root_tile.bounding_volume,
                                                 ctx.views_planes, ctx.views_vertices);

                computed_space_subset_intersections = true;
            }
            return intersects_space_subset_in;
        };

        // Forget about inactive views
        root_tile.should_render_children_in &= ctx.visiting_in;
        root_tile.render_children_in &= ctx.visiting_in;
        root_tile.render_self_in &= ctx.visiting_in;

        // If a tileset is standalone, we can wait until the root tile is
        // close and visible before loading and displaying it.
        // However, if it is an external tileset, the decision to load and
        // display the root tile belongs to the parent tileset. This is
        // controlled through the `render` value on the `Tileset` struct.
        if (tileset->is_external)
        {
            root_tile_needed_in |= ctx.visiting_in;
        }
        else
        {
            if (root_tile.load_status == ThreeDTile::LoadStatus::UNLOADED)
            {
                root_tile_needed_in |= ctx.visiting_in & above_max_screen_space_error_in
                    & get_intersects_space_subset_in() & !occluded_by_horizon_in;
            }
            else
            {
                if (below_min_screen_space_error_in.all_of(ctx.visiting_in))
                {
                    root_tile.delete_if_unused = true;
                }

                root_tile_needed_in |=
                    ctx.visiting_in & !below_min_screen_space_error_in & !occluded_by_horizon_in;
            }
        }

        if (root_tile_needed_in.any() && root_tile.load_status == ThreeDTile::LoadStatus::UNLOADED)
        {
            _load_tile(tileset, tileset->root_tile_index, root_tile, ctx);
        }

        auto render_self_in_before = root_tile.render_self_in;
        auto render_children_in_before = root_tile.render_children_in;

        if (root_tile.load_status == ThreeDTile::LoadStatus::RENDERABLE)
        {
            auto render_self_in =
                root_tile.render_none_in() & root_tile_needed_in & ctx.visiting_in;
            root_tile.set_render_self_in(render_self_in);
        }

        {
            auto render_none_in =
                root_tile.render_self_in & (!root_tile_needed_in) & ctx.visiting_in;
            root_tile.set_render_none_in(render_none_in);
        }

        if (render_self_in_before != root_tile.render_self_in
            || render_children_in_before != root_tile.render_children_in)
        {
            render_request.request_visual_render();
        }

        return render_request;
    }

    void _work_subtile_models(ThreeDTile::Subtile* subtile, const TilesetWorkContext& ctx)
    {
        std::visit(
            hrz::overload{
                [&](ThreeDTile::B3dmContent& content)
                {
                    if (content.prototype)
                    {
                        hrz::model::work(
                            content.prototype, ctx.al, ctx.js, ctx.ba, ctx.imgdec,
                            ctx.attributions);
                        content.materials.work(content.prototype);
                    }
                },
                [&](ThreeDTile::I3dmContent& content)
                {
                    if (content.prototype)
                    {
                        hrz::model::work(
                            content.prototype, ctx.al, ctx.js, ctx.ba, ctx.imgdec,
                            ctx.attributions);

                        if (content.model)
                        {
                            hrz::model::work(content.prototype, *content.model);
                        }
                    }
                },
                [&](ThreeDTile::TilesetContent&)
                {
                    // No action needed for TilesetContent
                },
                [&](ThreeDTile::PntsContent&)
                {
                    // No action needed for PntsContent
                },
                [&](std::monostate&)
                {
                    // No action needed for std::monostate
                }},
            subtile->content);
    }

    void _work_loading_tile(
        Tileset* tileset,
        uint32_t tile_index,
        ThreeDTile* tile,
        const TilesetWorkContext& ctx)
    {
        HRZ_SCOPED_SAMPLE_A("work loading tiles");

        if (!hrz::assets_loader::is_valid(ctx.al, tile->load_data_ticket))
        {
            // The data loading job may have been cancelled if the layer visibility has
            // been switched off. If we reach here now, it means that the visibility has
            // been turned back on. In that case, restart the loading job.
            _start_loading_tile_data(tileset, tile, ctx);
        }

        if (hrz::assets_loader::is_finished(ctx.al, tile->load_data_ticket))
        {
            if (hrz::assets_loader::get_status(ctx.al, tile->load_data_ticket)
                == hrz::assets_loader::RequestStatus::Loaded)
            {
                auto tile_mime_type = tile->range.has_value() && tile->range->mime_type.has_value()
                    ? std::string_view(tile->range->mime_type.value())
                    : hrz::assets_loader::get_content_type(ctx.al, tile->load_data_ticket);

                auto tile_data_blob =
                    hrz::assets_loader::get_blob(ctx.al, ctx.ba, tile->load_data_ticket);

                auto success = _decode_tile_data(
                    tile_index, *tile, tile_data_blob, tile_mime_type, tileset, ctx.al, ctx.js,
                    ctx.ba, ctx.attributions);

                hrz::assets_loader::end(ctx.al, tile->load_data_ticket);

                if (success)
                {
                    uint32_t batch_id_offset = 0;
                    for (auto& subtile : tile->subtiles)
                    {
                        subtile.batch_id_offset = batch_id_offset;
                        batch_id_offset += subtile.batch_length();
                    }
                    tile->subtiles_left_to_load = tile->subtiles.size();
                    tile->load_status = ThreeDTile::LoadStatus::LOADING_CONTENT;
                }
                else
                {
                    HRZ_LOG_ERROR(
                        "Could not decode tile data at \"{}\"",
                        tileset->base_url.derive(tile->uri));

                    tile->load_status = ThreeDTile::LoadStatus::ERROR;
                }
            }
            else
            {
                HRZ_LOG_ERROR(
                    "Could not load tile data at \"{}\"", tileset->base_url.derive(tile->uri));

                hrz::assets_loader::end(ctx.al, tile->load_data_ticket);

                tile->load_status = ThreeDTile::LoadStatus::ERROR;
            }
        }
        else if (ctx.should_update_priorities)
        {
            uint32_t priority =
                _compute_request_priority(*tile, *tileset->config, ctx.views_info, ctx.visiting_in);
            tile->load_data_ticket =
                hrz::assets_loader::reset_priority(ctx.al, tile->load_data_ticket, priority);
        }
    }

    static void _mark_subtile_load_error(ThreeDTile& tile, ThreeDTile::Subtile& subtile)
    {
        subtile.load_status = ThreeDTile::Subtile::LoadStatus::ERROR;
        tile.subtiles_left_to_load -= 1;
    }

    static void _mark_subtile_loaded(ThreeDTile& tile, ThreeDTile::Subtile& subtile)
    {
        subtile.load_status = ThreeDTile::Subtile::LoadStatus::LOADED;
        tile.subtiles_left_to_load -= 1;
    }

    hrz::RenderRequest _work_loading_subtile_b3dm(
        const Tileset& tileset,
        ThreeDTile& tile,
        ThreeDTile::Subtile& subtile,
        const hrz::picking::ObjectReference& obj_ref,
        const TilesetWorkContext& ctx)
    {
        assert(subtile.load_status == ThreeDTile::Subtile::LoadStatus::LOADING);

        hrz::RenderRequest render_request;
        auto& content = std::get<ThreeDTile::B3dmContent>(subtile.content);

        if (hrz_jobs::is_job_valid(ctx.js, content.decode_batch_table_ticket)
            && hrz_jobs::is_job_finished(ctx.js, content.decode_batch_table_ticket))
        {
            if (hrz_jobs::get_job_status(ctx.js, content.decode_batch_table_ticket)
                == hrz::job_scheduler::JobStatus::Finished_Success)
            {
                hrz::three_d_tiles::DecodedBatchTable response;
                hrz_jobs::get_job_response(ctx.js, content.decode_batch_table_ticket, response);

                subtile.attribute_values = std::move(response.attribute_values);
                subtile.batches_to_feature_ids = std::move(response.batches_to_feature_ids);

                // @Safety the lifetime of the hashes blob array data is
                // extended to the end of the create_batched_model_geometry,
                // which copies the content without keeping a reference to it.
                content.geometry = {hrz::model::create_batched_model_geometry(
                    content.prototype, obj_ref,
                    hrz::picking::FeatureReference(
                        _system_picking_id, tileset.config->layer_handle),
                    subtile.batch_id_offset, content.batch_length,
                    subtile.batches_to_feature_ids.hashes().get_data().unsafe_as_span())};

                hrz::model::set_batched_selection(
                    content.prototype, *content.geometry, tileset.config->selected_feature_ids);
            }
            else
            {
                _mark_subtile_load_error(tile, subtile);
            }
        }

        if (content.prototype && content.geometry)
        {
            render_request |= content.materials.update(content.prototype, *content.geometry);

            auto active_model = content.materials.get_active_model();
            if (active_model)
            {
                auto status = hrz::model::get_status(content.prototype, active_model.value());
                if (status == hrz::model::BakedModelStatus::Ready
                    || status == hrz::model::BakedModelStatus::ReadyWithErrors)
                {
                    if (status == hrz::model::BakedModelStatus::ReadyWithErrors)
                    {
                        HRZ_LOG_WARNING(
                            "Couldn't properly load material for \"{}\"",
                            tileset.base_url.derive(tile.uri));
                    }
                    _mark_subtile_loaded(tile, subtile);
                }
                else if (status == hrz::model::BakedModelStatus::Error)
                {
                    HRZ_LOG_ERROR(
                        "Could not load model at \"{}\"", tileset.base_url.derive(tile.uri));
                    _mark_subtile_load_error(tile, subtile);
                }
            }
        }
        return render_request;
    }

    hrz::RenderRequest _work_loading_subtile_i3dm(
        Tileset& tileset,
        ThreeDTile& tile,
        ThreeDTile::Subtile& subtile,
        const hrz::picking::ObjectReference& obj_ref,
        const TilesetWorkContext& ctx)
    {
        assert(subtile.load_status == ThreeDTile::Subtile::LoadStatus::LOADING);

        hrz::RenderRequest render_request;
        auto& content = std::get<ThreeDTile::I3dmContent>(subtile.content);

        if (content.prototype == nullptr && content.model_uri_hash.has_value())
        {
            auto it = _model_uris_to_prototypes.find(content.model_uri_hash.value());
            if (it == std::end(_model_uris_to_prototypes))
            {
                _mark_subtile_load_error(tile, subtile);
                return render_request;
            }

            auto& proto_ref = it->second;

            if (proto_ref.status == ModelPrototypeRef::Status::LOADED)
            {
                content.prototype = proto_ref.prototype;
                content.geometry = {proto_ref.geometry};
                content.material = {proto_ref.material};
                content.model = {proto_ref.model};
            }
            else if (proto_ref.status == ModelPrototypeRef::Status::ERROR)
            {
                _mark_subtile_load_error(tile, subtile);
                return render_request;
            }
        }
        else if (
            hrz_jobs::is_job_valid(ctx.js, content.decode_batch_table_ticket)
            && hrz_jobs::is_job_finished(ctx.js, content.decode_batch_table_ticket))
        {
            if (hrz_jobs::get_job_status(ctx.js, content.decode_batch_table_ticket)
                != hrz::job_scheduler::JobStatus::Finished_Success)
            {
                hrz_jobs::cancel_job(ctx.js, content.decode_batch_table_ticket);
                _mark_subtile_load_error(tile, subtile);
                return render_request;
            }

            assert(content.instance_group == std::nullopt);

            if (!content.model_uri_hash.has_value())
            {
                content.geometry = {create_instanced_model_geometry(content.prototype)};
                content.material = {hrz::model::create_model_material(content.prototype, {})};
                content.model = {hrz::model::create_baked_model(
                    content.prototype, *content.geometry, *content.material)};
            }

            hrz::three_d_tiles::DecodedBatchTable response;
            hrz_jobs::get_job_response(ctx.js, content.decode_batch_table_ticket, response);

            subtile.attribute_values = std::move(response.attribute_values);
            subtile.batches_to_feature_ids = std::move(response.batches_to_feature_ids);

            bool use_compressed_normals =
                !content.normal_oct32p_data.empty() && content.normal_data.empty();
            bool use_compressed_positions =
                !content.position_quantized_data.empty() && content.position_data.empty();

            hrz::model::InstanceGroupData group_data;
            group_data.use_enu_orientation = content.use_east_north_up_orientation;
            group_data.transform = tile.base_transform * subtile.rtc_transform;
            group_data.cull_modifier = my::CullModifier::Swap;
            group_data.scales = content.scale_data;
            group_data.colors = content.per_instance_color;
            group_data.object_ids = content.batch_id_data;
            group_data.feature_id_per_object =
                subtile.batches_to_feature_ids.hashes().get_data().unsafe_as_span();

            if (use_compressed_positions)
            {
                group_data.compressed_positions = content.position_quantized_data;
                group_data.position_compression.type = hrz::model::DracoCompressionType::Quantized;
                group_data.position_compression.quantization_mins =
                    lm::vec3(content.quantized_volume_offset);
                group_data.position_compression.quantization_scale =
                    lm::vec3(content.quantized_volume_scale) / 65535.0f;
            }
            else
            {
                group_data.positions = content.position_data;
                group_data.position_compression.type = hrz::model::DracoCompressionType::None;
            }

            if (use_compressed_normals)
            {
                group_data.compressed_normals = content.normal_oct32p_data;
                group_data.normal_compression.type = hrz::model::DracoCompressionType::OctEncoded;
                group_data.normal_compression.quantization_scale = lm::vec3(2.0f / 65535.0f);
            }
            else
            {
                group_data.normals = content.normal_data;
                group_data.normal_compression.type = hrz::model::DracoCompressionType::None;
            }

            content.instance_group = hrz::model::create_instance_group(
                content.prototype, obj_ref,
                hrz::picking::FeatureReference(_system_picking_id, tileset.config->layer_handle),
                subtile.batch_id_offset, group_data);

            hrz::model::set_instance_group_selection(
                content.prototype, content.instance_group.value(),
                tileset.config->selected_feature_ids);

            content.position_data.clear();
            content.position_data.shrink_to_fit();
            content.position_quantized_data.clear();
            content.position_quantized_data.shrink_to_fit();
            content.normal_data.clear();
            content.normal_data.shrink_to_fit();
            content.normal_oct32p_data.clear();
            content.normal_oct32p_data.shrink_to_fit();
            content.scale_data.clear();
            content.scale_data.shrink_to_fit();

            // Don't clear batch_id_data, we need it to expand the per batch colors
            // to per instance colors after styling!
        }

        return render_request;
    }

    hrz::RenderRequest _work_loading_subtile_pnts(
        Tileset& tileset,
        ThreeDTile& tile,
        ThreeDTile::Subtile& subtile,
        const hrz::picking::ObjectReference& obj_ref,
        const TilesetWorkContext& ctx)
    {
        assert(subtile.load_status == ThreeDTile::Subtile::LoadStatus::LOADING);

        hrz::RenderRequest render_request;
        auto& content = std::get<ThreeDTile::PntsContent>(subtile.content);

        if (auto& allocation = content.geometry->decompressed_colors_allocation;
            allocation.has_value())
        {
            assert(content.geometry->compressed_colors.has_value());

            switch (allocation->get_state(ctx.ba))
            {
                case hrz::BlobArrayAllocationState::Allocated:
                {
                    auto colors_array = allocation->to_array(ctx.ba);
                    colors_array.register_blob_owner(
                        ctx.ba,
                        {hrz::monitoring::systems::ThreeDTilesLayers,
                         tileset.config->global_layer_id});
                    colors_array.register_blob_metadata(
                        ctx.ba, "contents"_ss, "point cloud colors"_ss);

                    {
                        auto colors_data = colors_array.get_mutable_data();
                        auto compressed_colors_data =
                            content.geometry->compressed_colors->get_data();

                        for (size_t i = 0; i < content.geometry->point_count; ++i)
                        {
                            auto color = compressed_colors_data[i];
                            colors_data[i] = lm::ubvec3(
                                (color & 0xf800) >> 8, (color & 0x07e0) >> 3,
                                (color & 0x001f) << 3);
                        }
                    }

                    content.geometry->compressed_colors = std::nullopt;
                    content.geometry->decompressed_colors_allocation = std::nullopt;

                    content.geometry->colors = std::move(colors_array);
                    break;
                }
                case hrz::BlobArrayAllocationState::Error:
                {
                    HRZ_LOG_ERROR(
                        "Could not allocate color array for point cloud tile \"{}\"", tile.uri);
                    _mark_subtile_load_error(tile, subtile);
                    break;
                }
                default: break;
            }
        }

        if (auto& allocation = content.geometry->compressed_normals_allocation;
            allocation.has_value())
        {
            assert(content.geometry->normals.has_value());

            switch (allocation->get_state(ctx.ba))
            {
                case hrz::BlobArrayAllocationState::Allocated:
                {
                    auto compressed_normals_array = allocation->to_array(ctx.ba);
                    compressed_normals_array.register_blob_owner(
                        ctx.ba,
                        {hrz::monitoring::systems::ThreeDTilesLayers,
                         tileset.config->global_layer_id});
                    compressed_normals_array.register_blob_metadata(
                        ctx.ba, "contents"_ss, "point cloud normals"_ss);

                    {
                        auto compressed_normals_data = compressed_normals_array.get_mutable_data();
                        auto normals_data = content.geometry->normals->get_data();

                        for (size_t i = 0; i < content.geometry->point_count; ++i)
                        {
                            compressed_normals_data[i] =
                                (uint16_t)hrz::octahedral_compress_normal<8>(normals_data[i]);
                        }
                    }

                    content.geometry->normals = std::nullopt;
                    content.geometry->compressed_normals_allocation = std::nullopt;

                    content.geometry->compressed_normals = std::move(compressed_normals_array);
                    break;
                }
                case hrz::BlobArrayAllocationState::Error:
                {
                    HRZ_LOG_ERROR(
                        "Could not allocate normal array for point cloud tile \"{}\"", tile.uri);
                    _mark_subtile_load_error(tile, subtile);
                    break;
                }
                default: break;
            }
        }

        if (hrz_jobs::is_job_valid(ctx.js, content.decode_batch_table_ticket)
            && hrz_jobs::is_job_finished(ctx.js, content.decode_batch_table_ticket))
        {
            if (hrz_jobs::get_job_status(ctx.js, content.decode_batch_table_ticket)
                != hrz::job_scheduler::JobStatus::Finished_Success)
            {
                hrz_jobs::cancel_job(ctx.js, content.decode_batch_table_ticket);
                _mark_subtile_load_error(tile, subtile);
                return render_request;
            }

            hrz::three_d_tiles::DecodedBatchTable response;
            hrz_jobs::get_job_response(ctx.js, content.decode_batch_table_ticket, response);

            content.decode_batch_table_ticket = {};
            content.has_loaded_batch_table = true;
            subtile.attribute_values = std::move(response.attribute_values);
            subtile.batches_to_feature_ids = std::move(response.batches_to_feature_ids);

            content.uniform_data.mutate(
                [this, obj_ref, &subtile, &tileset](hrz::PointCloud::UniformData& data)
                {
                    data.object_reference = obj_ref.to_uvec2();
                    data.feature_reference = hrz::picking::FeatureReference(
                                                 _system_picking_id, tileset.config->layer_handle)
                                                 .to_uvec3();
                    data.object_id_offset = subtile.batch_id_offset;
                });
        }

        if (subtile.load_status != ThreeDTile::Subtile::LoadStatus::ERROR
            && !content.geometry->decompressed_colors_allocation.has_value()
            && !content.geometry->compressed_normals_allocation.has_value()
            && content.has_loaded_batch_table
            && (content.point_cloud || content.geometry->point_count == 0))
        {
            _mark_subtile_loaded(tile, subtile);
        }

        return render_request;
    }

    hrz::RenderRequest _work_subtile_styling(
        const Tileset* tileset,
        ThreeDTile& tile,
        ThreeDTile::Subtile& subtile,
        const TilesetWorkContext& ctx)
    {
        hrz::RenderRequest render_request;

        assert(_can_be_styled(subtile));

        if ((subtile.styling_status == ThreeDTile::StylingStatus::IDLE && !_is_styled(subtile))
            || tileset->needs_attribute_reload || tileset->needs_restyling)
        {
            // The tile fulfills the prerequisites for styling, and either:
            //  * has no style and is not currently being styled, or,
            //  * a restyling has been requested.
            // So let's start or restart the styling process.

            render_request |= _start_styling_process(
                *tileset->config, tile, subtile, tileset->needs_attribute_reload, ctx);
        }
        else if (subtile.styling_status == ThreeDTile::StylingStatus::WAITING_FOR_VECTOR_DATA_LAYER)
        {
            subtile.styling_status = _request_attribute_values(*tileset->config, subtile);
        }
        else if (subtile.styling_status == ThreeDTile::StylingStatus::WAITING_FOR_ATTRIBUTE_VALUES)
        {
            // Waiting for the attribute value message.
        }
        else if (subtile.styling_status == ThreeDTile::StylingStatus::WAITING_FOR_JOB)
        {
            assert(hrz_jobs::is_job_valid(ctx.js, subtile.style_job_ticket));

            if (hrz_jobs::is_job_finished(ctx.js, subtile.style_job_ticket))
            {
                if (hrz_jobs::get_job_status(ctx.js, subtile.style_job_ticket)
                    == hrz::job_scheduler::JobStatus::Finished_Success)
                {
                    _retrieve_style_data_from_job(subtile, ctx.js);
                }
                else
                {
                    HRZ_LOG_ERROR("Styling job error for tile \"{}\"", tile.uri);
                    _reset_subtile_style(subtile);
                    hrz_jobs::cancel_job(ctx.js, subtile.style_job_ticket);
                }

                subtile.styling_status = ThreeDTile::StylingStatus::WAITING_FOR_RESOURCE_UPDATE;
            }
        }

        return render_request;
    }

    void _update_external_tilesets_render_status(ThreeDTile& tile)
    {
        for (auto& subtile : tile.subtiles)
        {
            if (subtile.has_external_tileset())
            {
                auto& content = std::get<ThreeDTile::TilesetContent>(subtile.content);
                if (content.tileset_handle != 0)
                {
                    auto external_tileset = _tilesets.get_object(content.tileset_handle);
                    external_tileset->render = tile.render_self_in.any();
                }
            }
        }
    }

    // Second value is whether this tile should be erased from the active list
    std::pair<hrz::RenderRequest, bool> _work_active_tile(
        Tileset* tileset,
        TilesetH tileset_handle,
        uint32_t tile_index,
        std::vector<uint32_t>* tiles_to_refine,
        const TilesetWorkContext& ctx)
    {
        auto& config = *tileset->config;
        auto& tile = tileset->tiles.at(tile_index);

        hrz::RenderRequest render_request;
        bool erase = false;

        hrz::picking::ObjectReference tile_object_reference =
            make_object_reference(tileset_handle, tile_index);

        // Forget about inactive views
        tile.should_render_children_in &= ctx.visiting_in;
        tile.render_children_in &= ctx.visiting_in;
        tile.render_self_in &= ctx.visiting_in;

        for (size_t i = 0; i < hrz::SCENE_VIEW_COUNT; ++i)
        {
            if (!ctx.visiting_in.is_set(i))
            {
                tile.rendered_child_count_per_view[i] = 0;
            }
        }

        // Advance work on models
        for (auto& subtile : tile.subtiles)
        {
            _work_subtile_models(&subtile, ctx);
        }

        //
        // Tile data loading
        //

        if (tile.load_status == ThreeDTile::LoadStatus::LOAD_WHEN_INSIDE_VIEWER_VOLUME)
        {
            assert(tile.viewer_bounding_volume.has_value());

            for (const auto& view_info : ctx.views_info)
            {
                auto distance = hrz::three_d_tiles::distance(
                    tile.viewer_bounding_volume.value(), view_info.cam_view_info.cam.pos);

                if (distance <= 0)
                {
                    // One of the cameras is inside the viewer request
                    // volume, the tile's contents can be loaded.
                    _start_loading_tile_data(tileset, &tile, ctx);
                    break;
                }
            }
        }
        else if (tile.load_status == ThreeDTile::LoadStatus::LOADING_TILE)
        {
            _work_loading_tile(tileset, tile_index, &tile, ctx);
        }
        else if (tile.load_status == ThreeDTile::LoadStatus::LOADING_CONTENT)
        {
            HRZ_SCOPED_SAMPLE_A("work decoding tiles");

            if (tile.subtiles.empty() && !tile.has_unsupported_subtiles)
            {
                // An empty tile that has been set to the loading content state.
                // This means that it should wait for its [grand-]children to
                // load content before setting itself to loaded, in order to avoid
                // showing holes.
                // If all its children are loaded and renderable, the tile is itself
                // loaded.
                // If the tile has a greater geometric error than its parent (or
                // grand-parents), it means that the tile is purely structural and
                // should not be displayed, so the tile is left in the loading
                // content state all long as the children are not renderable.
                // If the tile has an expected lower geometric error than its parent,
                // either it has active children, meaning that it needs to be refined,
                // or it has no active children, and therefore doesn't need to be
                // refined. (The decision to load children took place between now
                // and the moment when it was put in the loading content state.)
                // If it needs to be refined, it means that the tile is on screen,
                // and the children will soon be ready. To avoid showing the empty
                // tile, it is left in the loading content state.
                // If the tile doesn't need to be refined, it's a regular empty tile
                // that is too far from the camera to show its children, so it's set
                // to loaded, in order to avoid blocking its parent tile from being
                // effectively refined.
                if (tile.renderable_child_count == tile.child_count
                    || (!tile.is_empty_structural && tile.active_child_count == 0))
                {
                    tile.load_status = ThreeDTile::LoadStatus::LOADED;
                }
            }
            else
            {
                for (auto& subtile : tile.subtiles)
                {
                    if (subtile.load_status != ThreeDTile::Subtile::LoadStatus::LOADING) continue;

                    render_request |= std::visit(
                        hrz::overload{
                            [&](ThreeDTile::B3dmContent&) {
                                return _work_loading_subtile_b3dm(
                                    *tileset, tile, subtile, tile_object_reference, ctx);
                            },
                            [&](ThreeDTile::TilesetContent& content)
                            {
                                auto tileset = _tilesets.get_object(content.tileset_handle);
                                auto root_tile_status = get_root_tile_status(tileset);
                                if (root_tile_status == ThreeDTile::LoadStatus::RENDERABLE)
                                {
                                    _mark_subtile_loaded(tile, subtile);
                                }
                                else if (root_tile_status == ThreeDTile::LoadStatus::ERROR)
                                {
                                    _mark_subtile_load_error(tile, subtile);
                                }
                                return hrz::RenderRequest{};
                            },
                            [&](ThreeDTile::I3dmContent&) {
                                return _work_loading_subtile_i3dm(
                                    *tileset, tile, subtile, tile_object_reference, ctx);
                            },
                            [&](ThreeDTile::PntsContent&) {
                                return _work_loading_subtile_pnts(
                                    *tileset, tile, subtile, tile_object_reference, ctx);
                            },
                            [&](std::monostate&) { return hrz::RenderRequest{}; }},
                        subtile.content);
                }

                if (tile.subtiles_left_to_load == 0)
                {
                    bool has_subtile_error = false;
                    for (const auto& subtile : tile.subtiles)
                    {
                        has_subtile_error |=
                            subtile.load_status == ThreeDTile::Subtile::LoadStatus::ERROR;
                    }
                    tile.load_status = has_subtile_error ? ThreeDTile::LoadStatus::ERROR
                                                         : ThreeDTile::LoadStatus::LOADED;
                }
            }
        }

        if (tile.load_status == ThreeDTile::LoadStatus::LOADED
            || tile.load_status == ThreeDTile::LoadStatus::RENDERABLE)
        {
            for (auto& subtile : tile.subtiles)
            {
                if (!_can_be_styled(subtile)) continue;
                render_request |= _work_subtile_styling(tileset, tile, subtile, ctx);
            }
        }

        //
        // Refinement
        //

        auto screen_space_errors = _compute_tile_screen_space_errors(
            tile, ctx.views_info, ctx.sses, config.max_screen_space_error);

        hrz::SceneViewBitset above_max_screen_space_error_in;
        hrz::SceneViewBitset below_min_screen_space_error_in;
        threshold_screen_space_error(
            ctx.views_info, screen_space_errors, &below_min_screen_space_error_in,
            &above_max_screen_space_error_in, tileset->config->screen_space_error_hysteresis);

        hrz::SceneViewBitset occluded_by_horizon_in;
        test_horizon_occlusion(
            ctx.views_info, ctx.horizon_cullers, tile.horizon_occlusion_point,
            &occluded_by_horizon_in);

        std::optional<hrz::SceneViewBitset> intersects_space_subset_opt = std::nullopt;

        auto do_compute_space_subset_intersections = [&]()
        {
            if (intersects_space_subset_opt.has_value())
            {
                return intersects_space_subset_opt.value();
            }

            intersects_space_subset_opt = {compute_space_subset_intersections(
                ctx.views_info, tile.bounding_volume, ctx.views_planes, ctx.views_vertices)};
            return intersects_space_subset_opt.value();
        };

        const bool debug_draw_horizon_occlusion_points =
            hrz::get_flag(hrz::Flag::DebugDrawHorizonOcclusionPoints);
        if (debug_draw_horizon_occlusion_points && tile.horizon_occlusion_point.has_value())
        {
            static constexpr lm::vec4 OccludedColor = {0.7f, 0.7f, 0.7f, 1};
            static constexpr lm::vec4 VisibleColor = {1, 0, 0, 1};

            hrz::debug_draw::points(
                tile.horizon_occlusion_point.value().m,
                occluded_by_horizon_in.any() ? OccludedColor : VisibleColor,
                hrz::debug_draw::Space::Ecef, hrz::debug_draw::Group_Vector);
        }

        // Apply a bit of hysteresis on the [un]refinement decision.
        // The values come from suggestions on the Cesium bug trackers,
        // such as here: https://github.com/CesiumGS/cesium/issues/3279
        if (tile.is_empty_structural || above_max_screen_space_error_in.any())
        {
            hrz::SceneViewBitset should_refine_in = ctx.visiting_in
                & (hrz::SceneViewBitset(tile.is_empty_structural)
                   | (above_max_screen_space_error_in & !occluded_by_horizon_in));

            // Don't refine tiles that have a viewer bounding volume we're not inside of.
            if (tile.viewer_bounding_volume.has_value())
            {
                const auto& bounding_volume = tile.viewer_bounding_volume.value();

                for (const auto& view_info : ctx.views_info)
                {
                    bool is_outside_viewer_volume =
                        hrz::three_d_tiles::distance(
                            bounding_volume, view_info.cam_view_info.cam.pos)
                        > 0;

                    if (is_outside_viewer_volume)
                    {
                        should_refine_in.reset((int)view_info.view);
                    }
                }
            }

            // @Todo Hide when outside viewer volume?

            if (should_refine_in.any() && tile.child_count > 0)
            {
                if (!tile.is_empty_structural)
                {
                    hrz::SceneViewBitset intersects_space_subset =
                        do_compute_space_subset_intersections();

                    should_refine_in &= intersects_space_subset;
                }

                if (should_refine_in.any() && tile.active_child_count < tile.child_count)
                {
                    tiles_to_refine->push_back(tile_index);
                }

                tile.should_render_children_in |= should_refine_in;
            }
        }

        if (!tile.is_empty_structural && (tile.should_render_children_in & ctx.visiting_in).any())
        {
            hrz::SceneViewBitset should_unrefine_in =
                (below_min_screen_space_error_in | occluded_by_horizon_in) & ctx.visiting_in;

            if (should_unrefine_in.none())
            {
                should_unrefine_in = (!do_compute_space_subset_intersections()) & ctx.visiting_in;
            }

            if (should_unrefine_in.any())
            {
                // Unrefine tiles
                tile.should_render_children_in &= !should_unrefine_in;

                if (tile.should_render_children_in.none())
                {
                    for (int i = 0; i < tile.child_count; ++i)
                    {
                        auto child_tile_index =
                            tileset->child_links.at(tile.first_child_link_index + i);
                        auto& child_tile = tileset->tiles.at(child_tile_index);

                        child_tile.delete_if_unused = true;
                    }
                }
            }
        }

        //
        // Render status update
        //

        if (tile.delete_if_unused && tile.render_none_in().all_of(ctx.visiting_in))
        {
            if ((tile.load_status == ThreeDTile::LoadStatus::RENDERABLE
                 || tile.load_status == ThreeDTile::LoadStatus::LOAD_WHEN_INSIDE_VIEWER_VOLUME)
                && tile_index != tileset->root_tile_index)
            {
                auto& parent_tile = tileset->tiles.at(tile.parent_index);
                parent_tile.renderable_child_count -= 1;
                assert(parent_tile.renderable_child_count >= 0);
            }

            _unload_tile(tileset, tile, ctx.al, ctx.js, ctx.ba);
            erase = true;

            if (tile_index != tileset->root_tile_index)
            {
                auto& parent_tile = tileset->tiles.at(tile.parent_index);
                parent_tile.active_child_count -= 1;
                assert(parent_tile.active_child_count >= 0);
            }
        }

        //
        // Children render status update
        //

        auto set_render_status_to_render_self =
            [this, &render_request](ThreeDTile& tile, hrz::SceneViewBitset b)
        {
            tile.set_render_self_in(b);
            _update_external_tilesets_render_status(tile);
            render_request.request_visual_render();
        };

        auto set_render_status_to_render_children =
            [this, &render_request](ThreeDTile& tile, hrz::SceneViewBitset b)
        {
            tile.set_render_children_in(b);
            _update_external_tilesets_render_status(tile);
            render_request.request_visual_render();
        };

        auto set_render_status_to_render_self_and_children =
            [this, &render_request](ThreeDTile& tile, hrz::SceneViewBitset b)
        {
            tile.set_render_self_and_children_in(b);
            _update_external_tilesets_render_status(tile);
            render_request.request_visual_render();
        };

        auto set_render_status_to_render_none =
            [this, &render_request](ThreeDTile& tile, hrz::SceneViewBitset b)
        {
            tile.set_render_none_in(b);
            _update_external_tilesets_render_status(tile);
            render_request.request_visual_render();
        };

        // Set children to render if they should be rendered
        // (wrt the distance to the camera), if they are available,
        // and if this tile is currently set to render itself.
        if (tile.renderable_child_count == tile.child_count)
        {
            hrz::SceneViewBitset start_rendering_children_in =
                tile.render_only_self_in() & tile.should_render_children_in & ctx.visiting_in;

            if (start_rendering_children_in.any())
            {
                assert(tile.child_count > 0);

                for (int i = 0; i < tile.child_count; ++i)
                {
                    auto child_tile_index =
                        tileset->child_links.at(tile.first_child_link_index + i);
                    auto& child_tile = tileset->tiles.at(child_tile_index);

                    set_render_status_to_render_self(child_tile, start_rendering_children_in);
                }

                for (size_t i = 0; i < hrz::SCENE_VIEW_COUNT; ++i)
                {
                    if (start_rendering_children_in.is_set(i))
                    {
                        tile.rendered_child_count_per_view[i] = tile.child_count;
                    }
                }

                switch (tile.refinement_type)
                {
                    case hrz::three_d_tiles::RefinementType::REPLACE:
                        set_render_status_to_render_children(tile, start_rendering_children_in);
                        break;
                    case hrz::three_d_tiles::RefinementType::ADD:
                        set_render_status_to_render_self_and_children(
                            tile, start_rendering_children_in);
                        break;
                    default: assert(false && "Unhandled case"); break;
                }

                if (tile_index != tileset->root_tile_index)
                {
                    auto& parent_tile = tileset->tiles.at(tile.parent_index);

                    for (size_t i = 0; i < hrz::SCENE_VIEW_COUNT; ++i)
                    {
                        if (start_rendering_children_in.is_set(i))
                        {
                            assert(parent_tile.rendered_child_count_per_view[i] > 0);
                            parent_tile.rendered_child_count_per_view[i] -= 1;
                        }
                    }
                }
            }
        }

        // Stop rendering children when we don't need to anymore
        // Check if no grand-children are currently rendered.
        {
            hrz::SceneViewBitset all_child_are_rendered_in;
            for (size_t i = 0; i < hrz::SCENE_VIEW_COUNT; ++i)
            {
                if (ctx.visiting_in.is_set(i)
                    && tile.rendered_child_count_per_view[i] == tile.child_count)
                {
                    all_child_are_rendered_in.set(i);
                }
            }

            hrz::SceneViewBitset stop_rendering_children_in = (!tile.should_render_children_in)
                & tile.render_children_in & all_child_are_rendered_in & ctx.visiting_in;

            if (stop_rendering_children_in.any())
            {
                for (int i = 0; i < tile.child_count; ++i)
                {
                    auto child_tile_index =
                        tileset->child_links.at(tile.first_child_link_index + i);
                    auto& child_tile = tileset->tiles.at(child_tile_index);

                    set_render_status_to_render_none(child_tile, stop_rendering_children_in);
                }

                set_render_status_to_render_self(tile, stop_rendering_children_in);

                for (size_t i = 0; i < hrz::SCENE_VIEW_COUNT; ++i)
                {
                    if (stop_rendering_children_in.is_set(i))
                    {
                        tile.rendered_child_count_per_view[i] = 0;
                    }
                }

                // If we started rendering self and it was not at all
                // drawn before, notify the parent.
                if (tile_index != tileset->root_tile_index)
                {
                    auto& parent_tile = tileset->tiles.at(tile.parent_index);

                    for (size_t i = 0; i < hrz::SCENE_VIEW_COUNT; ++i)
                    {
                        if (stop_rendering_children_in.is_set(i))
                        {
                            assert(
                                parent_tile.rendered_child_count_per_view[i]
                                < parent_tile.child_count);
                            parent_tile.rendered_child_count_per_view[i] += 1;
                        }
                    }
                }
            }
        }

        // Perform culling if the tile is set to render itself
        if (tile.render_self_in.any())
        {
            hrz::SceneViewBitset intersects_space_subset_in =
                do_compute_space_subset_intersections();
            tile.culled_in = (!intersects_space_subset_in) | occluded_by_horizon_in;
        }
        else
        {
            // If we are not trying to draw the tile, the culling bitset should be reset.
            // Otherwise, flickering would occur in situations where a parent tile asks for
            // a child to be drawn, but the work on that child has already been carried out
            // for the current frame, meaning that it is stuck with whatever cull state it
            // has been left in. If that state was "culled", then the child would not be
            // drawn until the next frame, leaving a hole during the current one.
            tile.culled_in.reset();
        }

        return std::make_pair(render_request, erase);
    }

    hrz::RenderRequest _work_loaded_tileset(
        Tileset* tileset,
        TilesetH tileset_handle,
        const TilesetWorkContext& ctx)
    {
        hrz::RenderRequest render_request;
        google::protobuf::Arena arena;

        auto* config = tileset->config;

        render_request |= _update_visibility_constraints(config, ctx.views_info);

        if (!config->is_visible || !config->visibility_constraints_result.satisfied_in)
            return render_request;

        assert(tileset->status == Tileset::Status::LOADED);

        if (config->renew_vector_data_layer_request)
        {
            if (config->vector_data_layer_status
                != TilesetConfig::VectorDataLayerStatus::UNREQUESTED)
            {
                _vector_data_channel.send(hrz::vector_data::messages::ReleaseLayerLoader{
                    config->vector_data_layer_request_id});
            }

            _vector_data_channel.send(hrz::vector_data::messages::LoadLayer{
                config->vector_data_layer_request_id, config->vector_data_layer_id});
            config->vector_data_layer_status = TilesetConfig::VectorDataLayerStatus::LOADING;

            config->renew_vector_data_layer_request = false;
        }

        render_request |= _work_loaded_root_tile(tileset, ctx);

        // We have to refine after iterating over the active tiles,
        // otherwise we would be insert into the set while iterating on it,
        // which is illegal because it invalidates the iterators.
        std::vector<uint32_t> tiles_to_refine;

        for (auto it = tileset->active_tiles.begin(); it != tileset->active_tiles.end();)
        {
            auto result = _work_active_tile(tileset, tileset_handle, *it, &tiles_to_refine, ctx);
            render_request |= result.first;

            if (result.second)
            {
                tileset->active_tiles.erase(it++);
            }
            else
            {
                ++it;
            }
        }

        for (auto tile_index : tiles_to_refine)
        {
            auto& tile = tileset->tiles.at(tile_index);

            // Refine tile
            for (int i = 0; i < tile.child_count; ++i)
            {
                auto child_tile_index = tileset->child_links.at(tile.first_child_link_index + i);
                auto& child_tile = tileset->tiles.at(child_tile_index);

                if (child_tile.load_status == ThreeDTile::LoadStatus::UNLOADED)
                {
                    _load_tile(tileset, child_tile_index, child_tile, ctx);
                    tile.active_child_count += 1;
                }
            }
        }

        tileset->needs_attribute_reload = false;
        tileset->needs_restyling = false;

        return render_request;
    }

    hrz::RenderRequest _work_loaded_tilesets(const WorkContext& ctx)
    {
        HRZ_SCOPED_SAMPLE("work loaded tilesets");

        if (hrz::get_flag(hrz::Flag::DebugFreeze3DTilesCulling)) return {};

        hrz::RenderRequest render_request;

        hrz::StaticVector<hrz::render::ScreenSpaceError, hrz::SCENE_VIEW_COUNT> sses;
        hrz::StaticVector<hrz::HorizonCuller, hrz::SCENE_VIEW_COUNT> horizon_cullers;

        for (const auto& view_info : ctx.views_info)
        {
            sses.push_back(hrz::render::ScreenSpaceError(
                view_info.cam_view_info.cam.fovy, (double)view_info.cam_view_info.viewport.size.y,
                view_info.cam_view_info.viewport.device_pixel_ratio));
            horizon_cullers.push_back(hrz::HorizonCuller(view_info.cam_view_info.cam.pos));
        }

        auto views_planes = _compute_views_planes(ctx.views_info);
        auto views_vertices = _compute_views_vertices(ctx.views_info);

        // Tile download priorities are computed using their relative position
        // from the camera. Because the camera can move, the priorities can
        // become outdated, so we update them regularly.
        bool should_update_priorities = false;
        {
            double duration = hrz::now_frame_ms() - _request_priority_timer_ms;
            if (duration >= REQUEST_PRIORITY_UPDATE_INTERVAL)
            {
                should_update_priorities = true;
            }
        }

        TilesetWorkContext tileset_ctx(ctx);
        tileset_ctx.sses = sses;
        tileset_ctx.horizon_cullers = horizon_cullers;
        tileset_ctx.views_planes = views_planes;
        tileset_ctx.views_vertices = views_vertices;
        tileset_ctx.should_update_priorities = should_update_priorities;

        for (auto handle : _loaded_tilesets)
        {
            auto* tileset = _tilesets.get_object(handle);
            const auto* config = tileset->config;
            tileset_ctx.visiting_in =
                config->visibility_constraints_result.satisfied_in & config->scene_views_bitset;
            render_request |= _work_loaded_tileset(tileset, handle, tileset_ctx);
        }

        return render_request;
    }

    void _work_shared_models(const WorkContext& ctx)
    {
        for (auto it = std::begin(_model_uris_to_prototypes);
             it != std::end(_model_uris_to_prototypes);)
        {
            auto& proto_ref = it->second;

            if (proto_ref.ref_count == 0)
            {
                if (proto_ref.status == ModelPrototypeRef::Status::LOADING)
                {
                    hrz::assets_loader::end(ctx.al, proto_ref.load_external_gltf_ticket);
                }
                else if (proto_ref.status == ModelPrototypeRef::Status::LOADED)
                {
                    hrz::model::destroy(proto_ref.prototype, proto_ref.geometry);
                    hrz::model::destroy(proto_ref.prototype, proto_ref.model);
                    hrz::model::destroy(proto_ref.prototype, proto_ref.material);
                    hrz::model::destroy(
                        proto_ref.prototype, ctx.al, ctx.js, ctx.ba, _removed_resources);
                }

                _model_uris_to_prototypes.erase(it++);

                continue;
            }

            if (proto_ref.status == ModelPrototypeRef::Status::NEW)
            {
                proto_ref.load_external_gltf_ticket = hrz::assets_loader::begin(
                    ctx.al, proto_ref.uri, proto_ref.headers, proto_ref.load_queue,
                    proto_ref.loading_priority, proto_ref.resource_owner);
                proto_ref.status = ModelPrototypeRef::Status::LOADING;
            }
            else if (proto_ref.status == ModelPrototypeRef::Status::LOADING)
            {
                if (!hrz::assets_loader::is_valid(ctx.al, proto_ref.load_external_gltf_ticket))
                {
                    HRZ_LOG_ERROR("Error: Could not download external glTF.");
                    proto_ref.status = ModelPrototypeRef::Status::ERROR;
                }
                else if (hrz::assets_loader::is_finished(
                             ctx.al, proto_ref.load_external_gltf_ticket))
                {
                    if (hrz::assets_loader::get_status(ctx.al, proto_ref.load_external_gltf_ticket)
                        == hrz::assets_loader::RequestStatus::Loaded)
                    {
                        auto gltf_blob = hrz::assets_loader::get_blob(
                            ctx.al, ctx.ba, proto_ref.load_external_gltf_ticket);
                        proto_ref.prototype = hrz::model::create_from_gltf_blob(
                            ctx.ba, ctx.attributions, gltf_blob, proto_ref.uri, proto_ref.base_url,
                            proto_ref.data_offset, proto_ref.headers,
                            proto_ref.additional_attribution, proto_ref.load_queue,
                            proto_ref.loading_priority, proto_ref.resource_owner);
                        proto_ref.geometry = create_instanced_model_geometry(proto_ref.prototype);
                        proto_ref.material =
                            hrz::model::create_model_material(proto_ref.prototype, {});
                        proto_ref.model = hrz::model::create_baked_model(
                            proto_ref.prototype, proto_ref.geometry, proto_ref.material);

                        proto_ref.status = ModelPrototypeRef::Status::LOADED;
                    }
                    else
                    {
                        HRZ_LOG_ERROR("Error: Could not load external glTF \"{}\"", proto_ref.uri);

                        proto_ref.status = ModelPrototypeRef::Status::ERROR;
                    }

                    hrz::assets_loader::end(ctx.al, proto_ref.load_external_gltf_ticket);
                }
                else if (proto_ref.status == ModelPrototypeRef::Status::LOADED)
                {
                    hrz::model::work(
                        proto_ref.prototype, ctx.al, ctx.js, ctx.ba, ctx.imgdec, ctx.attributions);
                }
            }

            it++;
        }
    }

    void _work_messages(const WorkContext& ctx)
    {
        auto get_tileset_for_layer_request = [&](uint64_t vector_data_layer_request_id)
        {
            TilesetH tileset_handle = vector_data_layer_request_id >> 32;
            return _tilesets.get_object(tileset_handle);
        };

        auto get_subtile_for_data_request = [&](uint64_t attribute_request_id)
            -> std::tuple<Tileset*, ThreeDTile*, ThreeDTile::Subtile*>
        {
            TilesetH tileset_handle = attribute_request_id >> 32;
            auto tileset = _tilesets.get_object(tileset_handle);

            if (tileset == nullptr) return {nullptr, nullptr, nullptr};

            uint32_t tile_index = (attribute_request_id & 0xffffff00) >> 8;
            uint32_t subtile_index = attribute_request_id & 0xff;

            if (tile_index >= tileset->tiles.size()) return {nullptr, nullptr, nullptr};

            auto& tile = tileset->tiles.at(tile_index);

            if (subtile_index >= tile.subtiles.size()) return {nullptr, nullptr, nullptr};

            return {tileset, &tile, &tile.subtiles.at(subtile_index)};
        };

        for (auto& generic_message : _vector_data_channel.receive())
        {
            namespace messages = hrz::vector_data::messages;
            std::visit(
                hrz::overload{
                    [&](messages::LayerModelUpdate& message)
                    {
                        auto tileset = get_tileset_for_layer_request(message.request_id);
                        if (tileset != nullptr)
                        {
                            tileset->config->vector_data_layer_status =
                                TilesetConfig::VectorDataLayerStatus::LOADED;
                            _trigger_restyling(tileset, true);
                        }
                    },
                    [&](messages::LayerModelError& message)
                    {
                        auto tileset = get_tileset_for_layer_request(message.request_id);
                        if (tileset != nullptr)
                        {
                            tileset->config->vector_data_layer_status =
                                TilesetConfig::VectorDataLayerStatus::ERROR;
                            _trigger_restyling(tileset, true);
                        }
                    },
                    [&](const messages::LayerNewData&)
                    {
                        // No-op
                    },
                    [&](messages::DataUpdate& message)
                    {
                        auto [tileset, tile, subtile] =
                            get_subtile_for_data_request(message.request_id);
                        if (subtile != nullptr)
                        {
                            auto config = tileset->config;

                            bool all_attributes_found = true;

                            for (unsigned int attribute_index = 0;
                                 attribute_index < config->attributes.size(); ++attribute_index)
                            {
                                const auto& attribute = config->attributes.at(attribute_index);

                                if (attribute.has_vector_data_layer_source()
                                    && subtile->attribute_values.size() > attribute_index)
                                {
                                    auto& all_values = std::get<
                                        hrz::InlinedVector<hrz::vector_data::AttributeValues, 16>>(
                                        message.data);
                                    bool values_found = false;

                                    for (auto& values : all_values)
                                    {
                                        if (values.attribute_id == attribute.attribute_id)
                                        {
                                            subtile->attribute_values.at(attribute_index) =
                                                std::move(values);
                                            values_found = true;
                                            break;
                                        }
                                    }

                                    subtile->attribute_attributions = message.attribution;

                                    if (!values_found)
                                    {
                                        all_attributes_found = false;
                                        break;
                                    }
                                }
                            }

                            if (all_attributes_found)
                            {
                                subtile->vector_data_attribute_status =
                                    ThreeDTile::Subtile::VectorDataAttributeStatus::LOADED;
                                subtile->has_received_vector_data_attributes_once = true;

                                if (subtile->styling_status
                                    == ThreeDTile::StylingStatus::WAITING_FOR_ATTRIBUTE_VALUES)
                                {
                                    if (_try_start_style_job(
                                            *tileset->config, *tile, *subtile, ctx.js))
                                    {
                                        subtile->styling_status =
                                            ThreeDTile::StylingStatus::WAITING_FOR_JOB;
                                    }
                                    else
                                    {
                                        // This can happen for example if not all the
                                        // attributes have been loaded from the tile data.
                                        subtile->styling_status =
                                            ThreeDTile::StylingStatus::WAITING_FOR_RESOURCE_UPDATE;
                                    }
                                }
                            }
                            else
                            {
                                subtile->vector_data_attribute_status =
                                    ThreeDTile::Subtile::VectorDataAttributeStatus::ERROR;

                                if (subtile->styling_status
                                    == ThreeDTile::StylingStatus::WAITING_FOR_ATTRIBUTE_VALUES)
                                {
                                    HRZ_LOG_ERROR(
                                        "Attribute request error for tile \"{}\"", tile->uri);

                                    for (unsigned int attribute_index = 0;
                                         attribute_index < config->attributes.size();
                                         ++attribute_index)
                                    {
                                        const auto& attribute =
                                            config->attributes.at(attribute_index);

                                        if (attribute.has_vector_data_layer_source()
                                            && subtile->attribute_values.size() > attribute_index)
                                        {
                                            subtile->attribute_values.at(attribute_index) =
                                                std::nullopt;
                                            subtile->attribute_attributions = {};
                                        }
                                    }

                                    subtile->vector_data_attribute_status =
                                        ThreeDTile::Subtile::VectorDataAttributeStatus::ERROR;
                                    subtile->styling_status =
                                        ThreeDTile::StylingStatus::WAITING_FOR_RESOURCE_UPDATE;
                                }
                            }

                            if (tile->load_status == ThreeDTile::LoadStatus::LOADED
                                || tile->load_status == ThreeDTile::LoadStatus::RENDERABLE)
                            {
                                _trigger_restyling(tileset, false);
                            }
                        }
                    },
                    [&](messages::DataError& message)
                    {
                        auto [tileset, tile, subtile] =
                            get_subtile_for_data_request(message.request_id);
                        if (subtile != nullptr)
                        {
                            subtile->vector_data_attribute_status =
                                ThreeDTile::Subtile::VectorDataAttributeStatus::ERROR;

                            if (tile->load_status == ThreeDTile::LoadStatus::LOADED
                                || tile->load_status == ThreeDTile::LoadStatus::RENDERABLE)
                            {
                                _trigger_restyling(tileset, false);
                            }
                        }
                    }},
                generic_message);
        }
    }

    hrz::RenderRequest work(
        hrz::AssetsLoader* al,
        hrz::JobScheduler* js,
        hrz::BlobAllocator* ba,
        hrz::ImageDecoder* imgdec,
        hrz::AttributionRegistry* attributions,
        std::span<const hrz::RenderViewInfo> views_info)
    {
        HRZ_SCOPED_SAMPLE("3D tiles system work");

        hrz::RenderRequest render_request;

        for (auto handle : _removed_tilesets)
        {
            _unload_tileset(handle, al, js, ba);
            render_request.request_visual_render();
        }
        _removed_tilesets.clear();

        _work_loading_tilesets(al, js, ba, views_info);

        WorkContext ctx;
        ctx.al = al;
        ctx.js = js;
        ctx.ba = ba;
        ctx.imgdec = imgdec;
        ctx.attributions = attributions;
        ctx.views_info = views_info;

        _work_shared_models(ctx);
        _work_messages(ctx);

        render_request |= _work_loaded_tilesets(ctx);

        return render_request;
    }

    bool is_working()
    {
        for (auto& handle : _loading_tilesets)
        {
            const auto& tileset = _tilesets.get_object(handle);
            if (tileset->config->is_visible
                && tileset->config->visibility_constraints_result.satisfied_in)
                return true;
        }

        for (auto& handle : _loaded_tilesets)
        {
            auto tileset = _tilesets.get_object(handle);
            if (!tileset->config->is_visible
                || !tileset->config->visibility_constraints_result.satisfied_in)
                continue;

            for (auto& tile_index : tileset->active_tiles)
            {
                const auto& tile = tileset->tiles.at(tile_index);
                if (tile.load_status != ThreeDTile::LoadStatus::LOADED
                    && tile.load_status != ThreeDTile::LoadStatus::RENDERABLE
                    && tile.load_status != ThreeDTile::LoadStatus::ERROR)
                {
                    return true;
                }

                for (auto& subtile : tile.subtiles)
                {
                    if (subtile.styling_status != ThreeDTile::StylingStatus::IDLE)
                    {
                        return true;
                    }
                }
            }
        }

        return false;
    }

    hrz::RenderRequest _upload_subtile_style_data(ThreeDTile::Subtile& subtile)
    {
        std::visit(
            hrz::overload{
                [&](ThreeDTile::B3dmContent& content)
                {
                    if (subtile.styling_status
                        == ThreeDTile::StylingStatus::WAITING_FOR_RESOURCE_UPDATE)
                    {
                        assert(content.prototype && content.geometry);

                        // There is at least one colour: if the tile does not have
                        // batch IDs, the whole geometry is treated as batch 0.
                        hrz::model::set_batched_colors(
                            content.prototype, *content.geometry, content.per_batch_color);

                        content.has_been_styled_once = true;
                        subtile.styling_status = ThreeDTile::StylingStatus::IDLE;
                    }
                },
                [&](ThreeDTile::I3dmContent& content)
                {
                    if (subtile.styling_status
                        == ThreeDTile::StylingStatus::WAITING_FOR_RESOURCE_UPDATE)
                    {
                        assert(content.prototype && content.instance_group);

                        hrz::model::set_instance_group_colors(
                            content.prototype, *content.instance_group, content.per_instance_color);

                        content.has_been_styled_once = true;
                        subtile.styling_status = ThreeDTile::StylingStatus::IDLE;
                    }
                },
                [&](ThreeDTile::PntsContent& content)
                {
                    if (subtile.styling_status
                        == ThreeDTile::StylingStatus::WAITING_FOR_RESOURCE_UPDATE)
                    {
                        assert(content.point_cloud);
                        content.point_cloud->update_feature_colors(
                            content.per_batch_color, content.has_transparent_feature);
                        content.has_been_styled_once = true;
                        subtile.styling_status = ThreeDTile::StylingStatus::IDLE;
                    }
                },
                [&](ThreeDTile::TilesetContent&)
                { subtile.styling_status = ThreeDTile::StylingStatus::IDLE; },
                [&](std::monostate&) { subtile.styling_status = ThreeDTile::StylingStatus::IDLE; }},
            subtile.content);

        return hrz::RenderRequest::visual();
    }

    void _make_subtile_renderable(
        const Tileset& tileset,
        const ThreeDTile& tile,
        ThreeDTile::Subtile& subtile)
    {
        std::visit(
            hrz::overload{
                [&](ThreeDTile::B3dmContent& content)
                {
                    if (tile.was_made_renderable_once) return;

                    if (content.prototype && content.geometry)
                    {
                        hrz::model::set_batched_selection(
                            content.prototype, *content.geometry,
                            tileset.config->selected_feature_ids);
                    }
                },
                [&](ThreeDTile::I3dmContent& content)
                {
                    if (content.prototype && content.instance_group.has_value()
                        && !tile.was_made_renderable_once)
                    {
                        hrz::model::set_instance_group_selection(
                            content.prototype, content.instance_group.value(),
                            tileset.config->selected_feature_ids);
                    }
                },
                [&](ThreeDTile::PntsContent& content)
                {
                    if (content.point_cloud)
                    {
                        content.point_cloud->update_selection(tileset.config->selected_feature_ids);
                    }
                },
                [&](ThreeDTile::TilesetContent& content)
                {
                    assert(
                        get_root_tile_status(_tilesets.get_object(content.tileset_handle))
                        == ThreeDTile::LoadStatus::RENDERABLE);
                },
                [&](std::monostate&)
                {
                    // Empty block for std::monostate
                }},
            subtile.content);
    }

    hrz::RenderRequest _make_tile_renderable(
        Tileset* tileset,
        uint32_t tile_index,
        ThreeDTile& tile)
    {
        if (!tile.was_made_renderable_once)
        {
            for (auto& subtile : tile.subtiles)
            {
                assert(_is_styled(subtile));
                _make_subtile_renderable(*tileset, tile, subtile);
            }
        }

#if DRAW_DEBUG_BOXES
        if (!tile.was_made_renderable_once)
        {
            RenderableBox renderable;

            {
                my::BufferResource vb_res(my::BufferResource::BufferType::Vertex);
                vb_res.size = tile.box.vertices.size() * sizeof(lm::vec3);
                vb_res.usage = my::UsageHint::Static;
                vb_res.data = (const void*)tile.box.vertices.data();

                renderable.pos_normal_buffer = render->rc->alloc(
                    &vb_res, hrz::monitoring::systems::ThreeDTilesLayers, tileset->config->layer_id,
                    {{"contents", "debug box vertex positions and normals"},
                     {"tile URI", tile.uri}});
            }

            {
                my::BufferResource vb_res(my::BufferResource::BufferType::Vertex);
                vb_res.size = tile.box.colors.size() * sizeof(lm::ubvec4);
                vb_res.usage = my::UsageHint::Static;
                vb_res.data = (const void*)tile.box.colors.data();

                renderable.color_buffer = render->rc->alloc(
                    &vb_res, hrz::monitoring::systems::ThreeDTilesLayers, tileset->config->layer_id,
                    {{"contents", "debug box vertex colors"}, {"tile URI", tile.uri}});
            }

            {
                my::VertexInputStream pos_stream;
                pos_stream.index = Box_PositionVertexInput;
                pos_stream.buffer = renderable.pos_normal_buffer;
                pos_stream.format = my::VertexFormat::Float32_3;
                pos_stream.offset = 0;
                pos_stream.stride = my::vertex_size(my::VertexFormat::Float32_3) * 2;
                pos_stream.rate = my::VertexRate::PerVertex;

                my::VertexInputStream normal_stream;
                normal_stream.index = Box_NormalVertexInput;
                normal_stream.buffer = renderable.pos_normal_buffer;
                normal_stream.format = my::VertexFormat::Float32_3;
                normal_stream.offset = my::vertex_size(my::VertexFormat::Float32_3);
                normal_stream.stride = my::vertex_size(my::VertexFormat::Float32_3) * 2;
                normal_stream.rate = my::VertexRate::PerVertex;

                my::VertexInputStream color_stream;
                color_stream.index = Box_ColorVertexInput;
                color_stream.buffer = renderable.color_buffer;
                color_stream.format = my::VertexFormat::UInt8Norm_4;
                color_stream.offset = 0;
                color_stream.stride = my::vertex_size(my::VertexFormat::UInt8Norm_4);
                color_stream.rate = my::VertexRate::PerVertex;

                my::VertexInputStream streams[] = {pos_stream, normal_stream, color_stream};

                my::VertexInputResource vi_res;
                vi_res.attrib_count = HRZ_ARRAY_COUNT(streams);
                vi_res.attribs = streams;
                my::ResourceHandle vertex_input = render->rc->alloc(
                    &vi_res, hrz::monitoring::systems::ThreeDTilesLayers,
                    tileset->config->layer_id);

                renderable.data.vertex_input = vertex_input;
            }

            renderable.data.vertex_count = 36;

            {
                BoxUniformData ubo_data;
                hrz::split_double(
                    tile.bounding_volume.center.x, ubo_data.center_low.x, ubo_data.center_high.x);
                hrz::split_double(
                    tile.bounding_volume.center.y, ubo_data.center_low.y, ubo_data.center_high.y);
                hrz::split_double(
                    tile.bounding_volume.center.z, ubo_data.center_low.z, ubo_data.center_high.z);
                ubo_data.transform = lm::mat4(hrz::remove_translation(tile.base_transform));
                ubo_data.normal_transform = tile.normal_transform;

                my::BufferResource ubo_res(my::BufferResource::BufferType::Uniform);
                ubo_res.size = sizeof(BoxUniformData);
                ubo_res.usage = my::UsageHint::Static;
                ubo_res.data = &ubo_data;

                renderable.data.ubo_buffer = render->rc->alloc(
                    &ubo_res, hrz::monitoring::systems::ThreeDTilesLayers,
                    tileset->config->layer_id,
                    {{"contents", "debug box uniforms"}, {"tile URI", tile.uri}});
            }

            renderable.data.shader = _box_shader;

            renderable.bin_mask = hrz::RenderWorldOpaqueBin;

            renderable.center = tile.bounding_volume.center;
            renderable.radius = tile.bounding_volume.radius;

            tile.renderable_box = renderable;
        }
#endif

        // If the tile has a viewer bounding volume, it has already incremented
        // the parent's renderable child count.
        if (!tile.was_made_renderable_once && tile_index != tileset->root_tile_index
            && !tile.viewer_bounding_volume.has_value())
        {
            auto& parent_tile = tileset->tiles.at(tile.parent_index);
            parent_tile.renderable_child_count += 1;
            assert(parent_tile.renderable_child_count <= parent_tile.child_count);
        }

        tile.load_status = ThreeDTile::LoadStatus::RENDERABLE;
        tile.was_made_renderable_once = true;

        return hrz::RenderRequest::visual();
    }

    void _work_gpu_subtile_i3dm(
        ThreeDTile& tile,
        ThreeDTile::Subtile& subtile,
        hrz::Render* render,
        hrz::BlobAllocator* ba)
    {
        auto& content = std::get<ThreeDTile::I3dmContent>(subtile.content);
        if (content.prototype != nullptr)
        {
            hrz::model::work_gpu(content.prototype, ba, render);

            if (content.model)
            {
                hrz::model::work_gpu(
                    content.prototype, *content.model, _model_shared_resources_instanced, render);
            }

            if (content.instance_group.has_value())
            {
                auto group_handle = content.instance_group.value();

                hrz::model::work_gpu(content.prototype, group_handle, render);

                if (subtile.load_status == ThreeDTile::Subtile::LoadStatus::LOADING)
                {
                    auto prototype_status = hrz::model::get_status(content.prototype);
                    auto model_status = hrz::model::get_status(content.prototype, *content.model);
                    auto group_status =
                        hrz::model::get_instance_group_status(content.prototype, group_handle);

                    if (group_status == hrz::model::InstanceGroupStatus::Ready
                        && prototype_status == hrz::model::ModelPrototypeStatus::Ready
                        && model_status == hrz::model::BakedModelStatus::Ready)
                    {
                        _mark_subtile_loaded(tile, subtile);
                    }
                    else if (
                        group_status == hrz::model::InstanceGroupStatus::Error
                        || prototype_status == hrz::model::ModelPrototypeStatus::Error
                        || model_status == hrz::model::BakedModelStatus::Error)
                    {
                        _mark_subtile_load_error(tile, subtile);
                    }
                }
            }
        }
    }

    static void _work_gpu_subtile_pnts(
        Tileset* tileset,
        ThreeDTile& tile,
        ThreeDTile::Subtile& subtile,
        hrz::Render* render)
    {
        auto& content = std::get<ThreeDTile::PntsContent>(subtile.content);

        // Already uploaded or empty
        if (content.point_cloud
            || (content.geometry.has_value() && content.geometry->point_count == 0))
            return;

        auto geometry_has_positions = [&]()
        {
            if (!content.geometry.has_value())
            {
                return false;
            }
            else if (content.geometry->positions_format == my::VertexFormat::Float32_3)
            {
                return std::get<hrz::BlobArray<lm::vec3>>(content.geometry->positions)
                    .blob()
                    .is_valid();
            }
            else if (content.geometry->positions_format == my::VertexFormat::UInt16Norm_3)
            {
                return std::get<hrz::BlobArray<lm::usvec3>>(content.geometry->positions)
                    .blob()
                    .is_valid();
            }
            else
            {
                return false;
            }
        };

        // Data not available yet
        if (!geometry_has_positions() || !content.has_loaded_batch_table) return;

        content.uniform_data.mutate(
            [&tile, &subtile, &content](hrz::PointCloud::UniformData& uniform)
            {
                lm::dmat4 transform = tile.base_transform * subtile.rtc_transform;
                uniform.linear_transform = lm::mat3(transform);
                uniform.normal_matrix = lm::mat3(hrz::compute_normal_transform_matrix(transform));

                lm::dvec3 offset = transform.w.xyz;
                hrz::split_double(offset.x, uniform.rtc_low.x, uniform.rtc_high.x);
                hrz::split_double(offset.y, uniform.rtc_low.y, uniform.rtc_high.y);
                hrz::split_double(offset.z, uniform.rtc_low.z, uniform.rtc_high.z);

                uniform.quantized_position_scale =
                    lm::vec3(content.geometry->quantized_volume_scale);
                uniform.quantized_position_offset =
                    lm::vec3(content.geometry->quantized_volume_offset);
            });

        content.geometry->feature_ids = subtile.batches_to_feature_ids.hashes();

        content.point_cloud = hrz::PointCloud::create(
            render, {hrz::monitoring::systems::ThreeDTilesLayers, tileset->config->global_layer_id},
            content.geometry.value(), content.uniform_data.get());

        content.point_cloud->update_feature_colors(
            content.per_batch_color, content.has_transparent_feature);

        assert(content.point_cloud);
        content.geometry = std::nullopt;
    }

    void _work_gpu_subtile(
        Tileset* tileset,
        ThreeDTile& tile,
        ThreeDTile::Subtile& subtile,
        hrz::Render* render,
        hrz::BlobAllocator* ba)
    {
        std::visit(
            hrz::overload{
                [&](ThreeDTile::B3dmContent& content)
                {
                    if (content.prototype != nullptr)
                    {
                        hrz::model::work_gpu(content.prototype, ba, render);
                        content.materials.work_gpu(
                            content.prototype, _model_shared_resources, render);
                    }
                },
                [&](ThreeDTile::I3dmContent&)
                { _work_gpu_subtile_i3dm(tile, subtile, render, ba); },
                [&](ThreeDTile::PntsContent&)
                { _work_gpu_subtile_pnts(tileset, tile, subtile, render); },
                [&](ThreeDTile::TilesetContent&)
                {
                    // No action needed for TilesetContent
                },
                [&](std::monostate&)
                {
                    // No action needed for std::monostate
                }},
            subtile.content);
    }

    hrz::RenderRequest work_gpu(hrz::Render* render, hrz::BlobAllocator* ba)
    {
        HRZ_SCOPED_SAMPLE("3D tiles system work gpu");
        assert(_model_shared_resources != nullptr && _model_shared_resources_instanced != nullptr);

        hrz::RenderRequest render_request;

        for (auto resource : _removed_resources)
        {
            render->rc->dealloc(resource);
        }
        _removed_resources.clear();

        for (auto handle : _loaded_tilesets)
        {
            auto tileset = _tilesets.get_object(handle);

            assert(tileset->status == Tileset::Status::LOADED);

            for (auto tile_index : tileset->active_tiles)
            {
                auto& tile = tileset->tiles.at(tile_index);

                // First advance work on models

                bool has_unstyled_subtiles = false;
                bool has_non_idle_styling_status_subtiles = false;

                for (auto& subtile : tile.subtiles)
                {
                    if (!_is_styled(subtile)) has_unstyled_subtiles = true;

                    if (subtile.styling_status != ThreeDTile::StylingStatus::IDLE)
                        has_non_idle_styling_status_subtiles = true;

                    _work_gpu_subtile(tileset, tile, subtile, render, ba);
                }

                // The tile has finished loading. We only make it renderable
                // once it is styled, so as to avoid having a restyling a few
                // frames later, which would create undesired flickering.
                if (tile.load_status == ThreeDTile::LoadStatus::LOADED && !has_unstyled_subtiles)
                {
                    render_request |= _make_tile_renderable(tileset, tile_index, tile);
                }

                if (has_non_idle_styling_status_subtiles)
                {
                    for (auto& subtile : tile.subtiles)
                    {
                        if (subtile.styling_status != ThreeDTile::StylingStatus::IDLE)
                        {
                            render_request |= _upload_subtile_style_data(subtile);
                        }
                    }
                }
            }
        }

        return render_request;
    }

    void draw(hrz::Render* render, hrz::AttributionRegistry* attributions)
    {
        HRZ_SCOPED_SAMPLE("3D tiles system draw");

        for (auto handle : _loaded_tilesets)
        {
            auto tileset = _tilesets.get_object(handle);
            auto config = tileset->config;

            assert(tileset->status == Tileset::Status::LOADED);

            for (auto tile_index : tileset->active_tiles)
            {
                auto& tile = tileset->tiles.at(tile_index);

                if (!config->is_visible || !tileset->render
                    || tile.load_status != ThreeDTile::LoadStatus::RENDERABLE)
                    continue;

                hrz::SceneViewBitset draw_in = tile.render_self_in & !tile.culled_in
                    & hrz::SceneViewBitset(config->visibility_constraints_result.satisfied_in
                                           & config->scene_views_bitset);

                if (!draw_in.any()) continue;

#if DRAW_DEBUG_BOXES
                render->rd->collect_renderable(tile.renderable_box.value());
#endif

                for (auto& subtile : tile.subtiles)
                {
                    if (subtile.attribute_attributions)
                    {
                        hrz::attribution::use_this_frame(
                            attributions, subtile.attribute_attributions);
                    }

                    std::visit(
                        hrz::overload{
                            [&](ThreeDTile::B3dmContent& content)
                            {
                                const auto& model = content.materials.get_active_model();
                                if (content.prototype && model.has_value())
                                {
                                    hrz::model::draw(
                                        content.prototype, model.value(), content.draw_prps,
                                        draw_in.bits(), _model_shared_resources, render,
                                        attributions);
                                }
                            },
                            [&](ThreeDTile::I3dmContent& content)
                            {
                                if (content.prototype && content.model.has_value())
                                {
                                    hrz::model::draw(
                                        content.prototype, content.model.value(),
                                        content.instance_group.value(), content.draw_prps,
                                        draw_in.bits(), _model_shared_resources_instanced, render,
                                        attributions);
                                }
                            },
                            [&](ThreeDTile::PntsContent& content)
                            {
                                if (content.point_cloud)
                                {
                                    if (content.uniform_data.is_dirty())
                                    {
                                        content.point_cloud->update_uniform_data(
                                            content.uniform_data.get());
                                    }

                                    content.point_cloud->draw(render, draw_in.bits());
                                }
                            },
                            [&](ThreeDTile::TilesetContent&)
                            {
                                // No action needed for these types
                            },
                            [&](std::monostate&)
                            {
                                // No action needed for these types
                            }},
                        subtile.content);
                }
            }
        }
    }

    LayerH get_layer_for_tileset(TilesetH tileset_handle)
    {
        auto tileset = _tilesets.get_object(tileset_handle);

        if (tileset == nullptr)
        {
            HRZ_LOG_WARNING("Unknown tileset handle: {}", tileset_handle);
            return 0;
        }

        return tileset->config->layer_handle;
    }

    std::string get_url_for_tile(TilesetH tileset_handle, uint32_t tile_index)
    {
        auto tileset = _tilesets.get_object(tileset_handle);

        if (tileset == nullptr)
        {
            HRZ_LOG_WARNING("Unknown tileset handle: {}", tileset_handle);
            return "";
        }

        if (tile_index >= tileset->tiles.size())
        {
            HRZ_LOG_WARNING("Invalid tile index for tileset {}: {}", tileset_handle, tile_index);
            return "";
        }

        const auto& tile = tileset->tiles.at(tile_index);

        return tileset->base_url.derive(tile.uri);
    }

    std::optional<hrz::vector_data::FeatureId> get_feature_id(
        TilesetH tileset_handle,
        uint32_t tile_index,
        uint32_t batch_id)
    {
        auto tileset = _tilesets.get_object(tileset_handle);

        if (tileset == nullptr)
        {
            HRZ_LOG_WARNING("Unknown tileset handle: {}", tileset_handle);
            return std::nullopt;
        }

        if (tile_index >= tileset->tiles.size())
        {
            HRZ_LOG_WARNING("Invalid tile index for tileset {}: {}", tileset_handle, tile_index);
            return std::nullopt;
        }

        const auto& tile = tileset->tiles.at(tile_index);

        for (const auto& subtile : tile.subtiles)
        {
            if (batch_id >= subtile.batch_id_offset
                && batch_id < subtile.batch_id_offset + subtile.batch_length())
            {
                if (subtile.batches_to_feature_ids.size() == 0)
                {
                    return std::nullopt; // No feature ids
                }

                assert(subtile.batches_to_feature_ids.size() == subtile.batch_length());
                return {subtile.batches_to_feature_ids.at(batch_id - subtile.batch_id_offset)};
            }
        }

        return std::nullopt;
    }

    unsigned int get_attribute_count(TilesetH tileset_handle)
    {
        auto tileset = _tilesets.get_object(tileset_handle);

        if (tileset == nullptr)
        {
            HRZ_LOG_WARNING("Unknown tileset handle: {}", tileset_handle);
            return 0;
        }

        return tileset->config->attributes.size();
    }

    struct AttributeValue
    {
        std::string_view name;
        hrz_proto::AttributeValue value;
    };

    std::optional<AttributeValue> get_attribute_value(
        TilesetH tileset_handle,
        uint32_t tile_index,
        uint32_t batch_id,
        unsigned int attribute_index)
    {
        auto tileset = _tilesets.get_object(tileset_handle);

        if (tileset == nullptr)
        {
            HRZ_LOG_WARNING("Unknown tileset handle: {}", tileset_handle);
            return std::nullopt;
        }

        if (tile_index >= tileset->tiles.size())
        {
            HRZ_LOG_WARNING("Invalid tile index for tileset {}: {}", tileset_handle, tile_index);
            return std::nullopt;
        }

        const auto& tile = tileset->tiles.at(tile_index);

        for (const auto& subtile : tile.subtiles)
        {
            if (batch_id < subtile.batch_id_offset
                || batch_id >= subtile.batch_id_offset + subtile.batch_length())
            {
                continue;
            }
            assert(batch_id < subtile.batch_length());

            if (attribute_index >= tileset->config->attributes.size())
            {
                HRZ_LOG_WARNING(
                    "Invalid attribute index for tileset {}: {}", tileset_handle, attribute_index);
                return std::nullopt;
            }

            if (attribute_index >= subtile.attribute_values.size())
            {
                return std::nullopt;
            }

            auto& values_opt = subtile.attribute_values.at(attribute_index);

            if (!values_opt.has_value())
            {
                return std::nullopt;
            }

            auto& values = values_opt.value();
            assert(values.values.size() == subtile.batch_length());

            const auto& attribute = tileset->config->attributes.at(attribute_index);

            auto value = hrz::vector_data::attr_from<hrz::vector_data::ApiAttributeValue>(
                values.get_reader().as_ref(batch_id - subtile.batch_id_offset));

            return {{attribute.name, value}};
        }

        return std::nullopt;
    }

    void destroy(
        hrz::AssetsLoader* al,
        hrz::JobScheduler* js,
        hrz::BlobAllocator* ba,
        hrz::Render* render)
    {
        for (auto handle : _removed_tilesets)
        {
            _unload_tileset(handle, al, js, ba);
        }
        _removed_tilesets.clear();

        while (!_loading_tilesets.empty())
        {
            _unload_tileset(*_loading_tilesets.begin(), al, js, ba);
        }
        _loading_tilesets.clear();

        while (!_loaded_tilesets.empty())
        {
            _unload_tileset(*_loaded_tilesets.begin(), al, js, ba);
        }
        _loaded_tilesets.clear();

        for (auto& it : _model_uris_to_prototypes)
        {
            if (it.second.prototype)
            {
                hrz::model::destroy(it.second.prototype, al, js, ba, _removed_resources);
            }
        }

        for (auto resource : _removed_resources)
        {
            render->rc->dealloc(resource);
        }
        _removed_resources.clear();

        if (_model_shared_resources != nullptr)
        {
            hrz::model::destroy_shared_resources(_model_shared_resources, render);
        }

        if (_model_shared_resources_instanced != nullptr)
        {
            hrz::model::destroy_shared_resources(_model_shared_resources_instanced, render);
        }

        _vector_data_channel.close();
    }
};

struct ArraySyncTraits
{
    enum ElementUpdateType
    {
        All,
        Palette,
    };

    using ContainerPath = hrz::scene_model::ThreeDTilesLayerPath;
    using ElementPath = hrz::scene_model::MaterialPath;

    static inline bool has_element_index(const ContainerPath& path)
    {
        return path.has_materials_index();
    }

    static inline size_t element_index(const ContainerPath& path) { return path.materials_index(); }

    static inline ElementPath element_path(const ContainerPath& path)
    {
        return path.clone().materials();
    }

    static inline ElementUpdateType element_update_type(const ElementPath& path)
    {
        if (path.is_data_texture_palette())
        {
            return ElementUpdateType::Palette;
        }
        else
        {
            return ElementUpdateType::All;
        }
    }
};

struct Layer
{
    uint64_t global_id;

    TilesetH tileset;

    bool source_updated = false;
    bool transform_updated = false;
    bool max_screen_space_error_updated = false;
    bool refinement_hysteresis_updated = false;
    bool id_attribute_name_updated = false;
    bool special_attributes_updated = false;
    bool vector_data_layer_id_updated = false;
    bool attributes_updated = false;
    bool palettes_updated = false;
    bool rng_seed_updated = false;
    bool styling_script_updated = false;
    bool visibility_updated = false;
    bool loading_priority_updated = false;
    bool visibility_constraints_updated = false;
    bool scene_views_updated = false;
    bool appearance_updated = false;
    bool http_headers_updated = false;
    bool material_properties_updated = false;
    bool active_materials_updated = false;

    using ArraySync = hrz::scene_model::ArraySync<ArraySyncTraits>;

    // Number of materials currently in the 3DTiles.
    size_t material_count = 0;
    ArraySync materials_array_sync;
};
} // namespace

namespace hrz
{
struct ThreeDTilesLayerSystem
{
    using IndexPool = GenIndexPool<LayerH, 8, 16>;
    using LayerPool = GenObjectPool<Layer, IndexPool, 64>;

    LayerPool layer_pool;
    hrz::flat_hash_map<uint64_t, LayerH> global_layer_id_to_handle;
    hrz::flat_hash_map<LayerH, uint64_t> layer_handles_to_global_id;

    std::vector<uint64_t> unregistered_layers;

    uint8_t system_picking_id;

    ThreeDTilesSystem three_d_tiles;
};

namespace three_d_tiles_layers
{
namespace
{
Layer* _get_layer(ThreeDTilesLayerSystem* system, uint64_t global_layer_id)
{
    auto it = system->global_layer_id_to_handle.find(global_layer_id);
    if (it == system->global_layer_id_to_handle.end())
    {
        return nullptr;
    }
    return system->layer_pool.get_object(it->second);
}

void _unregister_layers(
    ThreeDTilesLayerSystem* system,
    SceneModel* model,
    AssetsLoader* al,
    JobScheduler* js)
{
    assert(system && model && al && js);

    for (auto global_layer_id : system->unregistered_layers)
    {
        auto it = system->global_layer_id_to_handle.find(global_layer_id);
        if (it != system->global_layer_id_to_handle.end())
        {
            hrz_proto::PathRoot root;
            root.mutable_three_d_tiles_layer()->set_opaque(global_layer_id);
            scene_model::unregister_element(model, root);

            auto layer_handle = it->second;
            auto layer = system->layer_pool.get_object(layer_handle);

            if (layer)
            {
                if (layer->tileset != 0)
                {
                    system->three_d_tiles.remove_tileset(layer->tileset);
                }

                system->layer_pool.release(layer_handle);
            }

            system->global_layer_id_to_handle.erase(it);
            system->layer_handles_to_global_id.erase(layer_handle);
        }
    }

    system->unregistered_layers.clear();
}

RenderRequest _update_layer(
    ThreeDTilesLayerSystem* system,
    uint64_t global_layer_id,
    SceneModel* model,
    const SelectionSystem* selection,
    AssetsLoader* al,
    AttributionRegistry* attributions)
{
    RenderRequest render_request;

    auto layer = _get_layer(system, global_layer_id);
    if (!layer) return render_request;

    hrz::SceneModelAccessor accessor(model);

    hrz_proto::LayerHandle scene_model_handle;
    scene_model_handle.set_opaque(global_layer_id);

    hrz_proto::ThreeDTilesLayerPathBuilder<hrz::SceneModelAccessor> builder(
        accessor, scene_model_handle);

    LayerH internal_layer_handle = system->global_layer_id_to_handle.at(global_layer_id);

    bool reload_tileset = layer->source_updated || layer->transform_updated
        || layer->id_attribute_name_updated || layer->attributes_updated
        || layer->special_attributes_updated;

    if (layer->http_headers_updated && !reload_tileset)
    {
        auto headers = hrz::assets_loader::from_proto(builder.clone().http_headers().get());
        if (system->three_d_tiles.set_http_headers(layer->tileset, headers))
        {
            layer->source_updated = true;
            reload_tileset = true;
        }
    }

    if (reload_tileset)
    {
        if (layer->tileset != 0)
        {
            system->three_d_tiles.remove_tileset(layer->tileset);
        }

        auto new_model = builder.clone().get();

        auto get_transform = [&]()
        {
            auto transform_matrix = to_lm(new_model.transform());
            auto geo_matrix = new_model.apply_geographic_position()
                ? enu_to_ecef_transform_for_geo(from_proto(new_model.geographic_position()))
                : lm::dmat4::identity();
            return geo_matrix * transform_matrix;
        };

        const auto& new_url = new_model.url();
        const auto& headers = hrz::assets_loader::from_proto(new_model.http_headers());
        auto attribution =
            attribution::register_attribution(attributions, {new_model.attribution(), ""});
        bool preserve_query_parameters = new_model.preserve_query_parameters();
        auto transform = get_transform();
        double max_screen_space_error = new_model.max_screen_space_error();
        double refinement_hysteresis = new_model.refinement_hysteresis();
        bool is_visible = new_model.visible();
        int8_t loading_priority = clamp_cast<int32_t, int8_t>(new_model.loading_priority());
        int32_t clip_id = new_model.clip_id();
        uint32_t scene_views_bitset = new_model.scene_views().bits();
        hrz::render::LightingSettings lighting_settings =
            hrz::render::from_proto(new_model.lighting());
        hrz_proto::LayerVisibilityConstraintList visibility_constraints =
            new_model.visibility_constraints();
        bool draw_under_flat_overlays = new_model.draw_under_flat_overlays();
        uint32_t color_blend_mode = new_model.material_properties().feature_color_blend_mode();
        float color_blend_strength = new_model.material_properties().feature_color_blend_strength();

        unsigned int attribute_count = new_model.attributes_size();
        std::vector<hrz::three_d_tiles::AttributeConfig> attributes;
        attributes.reserve(attribute_count);

        for (unsigned int i = 0; i < attribute_count; ++i)
        {
            const auto& attribute = new_model.attributes(i);

            switch (attribute.source())
            {
                case hrz_proto::ThreeDTileAttributeSource::BATCH_TABLE_SOURCE:
                    attributes.push_back(
                        {attribute.name(),
                         hrz::three_d_tiles::AttributeConfig::BatchTable{attribute.is_feature_id()},
                         attribute.vector_data_attr_id(), style::Parser::INSERT_ERROR,
                         attribute.transform()});
                    break;
                case hrz_proto::ThreeDTileAttributeSource::VECTOR_DATA_LAYER_SOURCE:
                    attributes.push_back(
                        {attribute.name(), hrz::three_d_tiles::AttributeConfig::VectorDataLayer{},
                         attribute.vector_data_attr_id(), style::Parser::INSERT_ERROR,
                         attribute.transform()});
                    break;
                case hrz_proto::ThreeDTileAttributeSource::BATCH_CLASS_ID_SOURCE:
                    attributes.push_back(
                        {attribute.name(),
                         hrz::three_d_tiles::AttributeConfig::BatchClassId{
                             attribute.is_feature_id()},
                         attribute.vector_data_attr_id(), style::Parser::INSERT_ERROR,
                         attribute.transform()});
                    break;
                case hrz_proto::ThreeDTileAttributeSource::BATCH_CLASS_NAME_SOURCE:
                    attributes.push_back(
                        {attribute.name(),
                         hrz::three_d_tiles::AttributeConfig::BatchClassName{
                             attribute.is_feature_id()},
                         attribute.vector_data_attr_id(), style::Parser::INSERT_ERROR,
                         attribute.transform()});
                    break;
                default: assert(false && "Unhandled case"); break;
            }
        }

        layer->tileset = system->three_d_tiles.add_tileset(
            new_url, headers, attribution, preserve_query_parameters, transform,
            max_screen_space_error, refinement_hysteresis, attributes, internal_layer_handle,
            global_layer_id, is_visible, loading_priority, clip_id, scene_views_bitset,
            color_blend_mode, color_blend_strength, lighting_settings, visibility_constraints,
            draw_under_flat_overlays, al);
    }

    if (reload_tileset || layer->vector_data_layer_id_updated)
    {
        system->three_d_tiles.set_vector_data_layer_id(
            layer->tileset, builder.clone().vector_data_layer_id().get());
    }

    if (reload_tileset || layer->styling_script_updated || layer->palettes_updated
        || layer->rng_seed_updated)
    {
        auto layer_data = builder.clone().get();

        // @Todo It would be nice to have a way to access arrays without
        // having to get the parent object.

        system->three_d_tiles.set_style(
            layer->tileset, layer_data.styling_script(),
            {layer_data.palettes().data(), (size_t)layer_data.palettes_size()},
            layer_data.rng_seed());
    }

    if (reload_tileset || layer->max_screen_space_error_updated)
    {
        if (layer->tileset != 0)
        {
            double max_screen_space_error = builder.clone().max_screen_space_error().get();
            system->three_d_tiles.set_max_screen_space_error(
                layer->tileset, max_screen_space_error);
        }
    }

    if (reload_tileset || layer->refinement_hysteresis_updated)
    {
        if (layer->tileset != 0)
        {
            double refinement_hysteresis = builder.clone().refinement_hysteresis().get();
            system->three_d_tiles.set_refinement_hysteresis(layer->tileset, refinement_hysteresis);
        }
    }

    if (reload_tileset || layer->visibility_updated)
    {
        bool is_visible = builder.clone().visible().get();
        system->three_d_tiles.set_visibility(layer->tileset, is_visible, al);
        render_request.request_visual_render();
    }

    if (reload_tileset || layer->loading_priority_updated)
    {
        int8_t loading_priority =
            clamp_cast<int32_t, int8_t>(builder.clone().loading_priority().get());
        system->three_d_tiles.set_loading_priority(layer->tileset, loading_priority);
    }

    if (reload_tileset || layer->scene_views_updated)
    {
        system->three_d_tiles.set_scene_views_bitset(
            layer->tileset, builder.clone().scene_views().bits().get());
        render_request.request_visual_render();
    }

    if (reload_tileset || layer->appearance_updated)
    {
        int32_t clip_id = builder.clone().clip_id().get();
        hrz::render::LightingSettings lighting_settings =
            hrz::render::from_proto(builder.clone().lighting().get());
        bool draw_under_flat_overlays = builder.clone().draw_under_flat_overlays().get();

        system->three_d_tiles.update_appearance(
            layer->tileset, clip_id, lighting_settings, draw_under_flat_overlays);
        render_request.request_visual_render();
    }

    if (reload_tileset || layer->visibility_constraints_updated)
    {
        hrz_proto::LayerVisibilityConstraintList visibility_constraints =
            builder.clone().visibility_constraints().get();
        system->three_d_tiles.set_visibility_constraints(layer->tileset, visibility_constraints);
        render_request.request_visual_render();
    }

    bool reload_subtiles_content = false;

    auto recreate_all_materials = [&]()
    {
        // There is no way to retrieve an entire array _only_ so... Anyway it's
        // not like we're supposed to do this every frame.
        auto layer_proto = builder.clone().get();

        system->three_d_tiles.recreate_all_materials(
            layer->tileset,
            std::span<const hrz_proto::Material* const>(
                layer_proto.materials().data(), layer_proto.materials().size()));

        reload_subtiles_content = true;
        layer->material_count = layer_proto.materials_size();
    };

    if (reload_tileset)
    {
        recreate_all_materials();
        layer->materials_array_sync.reset(layer->material_count);
    }
    else
    {
        layer->materials_array_sync.synchronize(
            [&](const Layer::ArraySync::Command& cmd) -> size_t
            {
                switch (cmd.type)
                {
                    case Layer::ArraySync::Command::Add:
                    {
                        auto material =
                            builder.clone().materials((uint32_t)cmd.info.add.index_auth).get();
                        system->three_d_tiles.add_material(layer->tileset, material);
                        reload_subtiles_content = true;
                        layer->material_count += 1;
                        break;
                    }
                    case Layer::ArraySync::Command::RebuildAll:
                    {
                        recreate_all_materials();
                        break;
                    }
                    case Layer::ArraySync::Command::Remove:
                    {
                        system->three_d_tiles.remove_material(
                            layer->tileset, cmd.info.remove.index_mirror);
                        reload_subtiles_content = true;
                        layer->material_count -= 1;
                        break;
                    }
                    case Layer::ArraySync::Command::Update:
                    {
                        switch (cmd.info.update.update_type)
                        {
                            case ArraySyncTraits::ElementUpdateType::All:
                            {
                                auto material = builder.clone()
                                                    .materials((uint32_t)cmd.info.update.index_auth)
                                                    .get();
                                system->three_d_tiles.update_material(
                                    layer->tileset, cmd.info.update.index_mirror, material);
                                reload_subtiles_content = true;
                                break;
                            }
                            case ArraySyncTraits::ElementUpdateType::Palette:
                            {
                                auto palette = builder.clone()
                                                   .materials((uint32_t)cmd.info.update.index_auth)
                                                   .data_texture_palette()
                                                   .get();
                                system->three_d_tiles.update_material_palette(
                                    layer->tileset, cmd.info.update.index_mirror, palette);
                                render_request.request_visual_render();
                                break;
                            }
                        }
                        break;
                    }
                }
                return layer->material_count;
            });
    }

    if (reload_tileset || layer->material_properties_updated)
    {
        auto prps = builder.clone().material_properties().get();
        system->three_d_tiles.update_material_properties(layer->tileset, prps);
        render_request.request_visual_render();
        layer->material_properties_updated = false;
    }

    if (reload_tileset || layer->active_materials_updated)
    {
        auto prps = builder.clone().material_properties().get();
        system->three_d_tiles.update_active_materials(
            layer->tileset, prps.base_material(),
            prps.enable_overlay() ? prps.overlay_material() : std::optional<std::string_view>{});
        reload_subtiles_content = true;
        layer->active_materials_updated = false;
    }

    if (reload_tileset || reload_subtiles_content)
    {
        system->three_d_tiles.reload_subtiles_content(layer->tileset);
    }

    // There is no function to update the root transform of the tileset
    // without reloading it. Root transforms are rarely used, especially
    // for big tilesets, and changing the transform for an existing layer
    // even less.
    // As it would be a bother to implement root transform updates, I have
    // just taken the easy way.
    //      -tpetillon, 2020-10-09

    layer->source_updated = false;
    layer->transform_updated = false;
    layer->max_screen_space_error_updated = false;
    layer->refinement_hysteresis_updated = false;
    layer->id_attribute_name_updated = false;
    layer->special_attributes_updated = false;
    layer->vector_data_layer_id_updated = false;
    layer->attributes_updated = false;
    layer->palettes_updated = false;
    layer->rng_seed_updated = false;
    layer->styling_script_updated = false;
    layer->visibility_updated = false;
    layer->loading_priority_updated = false;
    layer->visibility_constraints_updated = false;
    layer->scene_views_updated = false;
    layer->appearance_updated = false;
    layer->http_headers_updated = false;
    layer->active_materials_updated = false;
    layer->material_properties_updated = false;

    if (reload_tileset
        || (hrz::selection::has_changed_since_last_frame(selection)
            && hrz::selection::has_changed_since_last_frame(selection, global_layer_id)))
    {
        size_t count = hrz::selection::selected_objects_count(selection, global_layer_id);
        std::vector<vector_data::FeatureIdHash> selected_feature_ids(count);
        hrz::selection::get_selected_objects(
            selection, global_layer_id,
            std::span<vector_data::FeatureIdHash>(selected_feature_ids));

        system->three_d_tiles.clear_selected_features(layer->tileset);
        system->three_d_tiles.add_selected_features(layer->tileset, selected_feature_ids);
        render_request.request_visual_render();
    }

    return render_request;
}
} // namespace

ThreeDTilesLayerSystem* create_system(
    PickingIdAllocator* picking_id_allocator,
    VectorDataLoader* vdl)
{
    auto system = new ThreeDTilesLayerSystem();
    system->system_picking_id = picking::allocate_system_id(picking_id_allocator);
    system->three_d_tiles.init(system->system_picking_id, vector_data::create_channel(vdl));

    return system;
}

void destroy_system(
    ThreeDTilesLayerSystem* system,
    AssetsLoader* al,
    JobScheduler* js,
    BlobAllocator* ba,
    Render* render,
    PickingIdAllocator* pia,
    SceneModel* scene_model)
{
    assert(system && al && js && ba && render && pia && scene_model);

    for (auto [global_layer_id, _] : system->global_layer_id_to_handle)
    {
        unregister_layer(system, global_layer_id);
    }
    _unregister_layers(system, scene_model, al, js);

    system->three_d_tiles.destroy(al, js, ba, render);

    picking::release_system_id(pia, system->system_picking_id);

    delete system;
}

void initialize_rendering(ThreeDTilesLayerSystem* system, Render* render)
{
    assert(system && render);

    system->three_d_tiles.initialize_rendering(render);
}

void register_layer(ThreeDTilesLayerSystem* system, SceneModel* model, uint64_t global_layer_id)
{
    assert(system && model);

    if (system->global_layer_id_to_handle.count(global_layer_id) == 0)
    {
        auto layer_handle = system->layer_pool.alloc();
        auto layer = system->layer_pool.get_object(layer_handle);
        layer->global_id = global_layer_id;
        layer->tileset = 0;

        system->global_layer_id_to_handle.insert({global_layer_id, layer_handle});
        system->layer_handles_to_global_id.insert({layer_handle, global_layer_id});

        hrz_proto::PathRoot root;
        root.mutable_three_d_tiles_layer()->set_opaque(global_layer_id);
        scene_model::register_element(model, root);

        // Default data
        hrz_proto::ThreeDTilesLayer data;
        data.set_url("");
        data.set_visible(true);
        data.set_max_screen_space_error(16.0f);
        data.set_refinement_hysteresis(0.3f);

        auto* transform = data.mutable_transform();
        transform->mutable_scale()->set_x(1.0f);
        transform->mutable_scale()->set_y(1.0f);
        transform->mutable_scale()->set_z(1.0f);
        transform->mutable_rotation()->set_w(1.0f);
        transform->mutable_frame()->set_front(hrz_proto::Axis::POS_Y);
        transform->mutable_frame()->set_up(hrz_proto::Axis::POS_Z);
        transform->mutable_frame()->set_handedness(hrz_proto::Handedness::RIGHT);

        data.set_apply_geographic_position(false);
        data.set_vector_data_layer_id(0);
        data.set_rng_seed(0);
        data.set_styling_script("");
        data.mutable_scene_views()->set_bits((1u << hrz::SCENE_VIEW_COUNT) - 1);
        data.set_clip_id(-1);
        data.mutable_lighting()->set_enable_lighting(true);
        data.mutable_lighting()->set_cast_shadows(true);
        data.mutable_lighting()->set_receive_shadows(true);
        data.mutable_material_properties()->set_feature_color_blend_mode(hrz_proto::BLEND_MULTIPLY);
        data.mutable_material_properties()->set_feature_color_blend_strength(1.0);

        hrz::SceneModelAccessor accessor(model);
        hrz_proto::ThreeDTilesLayerPathBuilder<hrz::SceneModelAccessor> builder(
            accessor, root.three_d_tiles_layer());
        builder.set(data);
    }
}

void unregister_layer(ThreeDTilesLayerSystem* system, uint64_t layer_id)
{
    assert(system);

    system->unregistered_layers.push_back(layer_id);
}

void notify_update(
    ThreeDTilesLayerSystem* system,
    uint64_t layer_id,
    scene_model::UpdateType update_type,
    const scene_model::ThreeDTilesLayerPath& path)
{
    auto layer = _get_layer(system, layer_id);
    if (!layer) return;

    layer->source_updated |= path.leaf() || path.is_url() || path.is_preserve_query_parameters()
        || path.is_attribution();
    layer->transform_updated |= path.leaf() || path.is_transform() || path.is_geographic_position()
        || path.is_apply_geographic_position();
    layer->max_screen_space_error_updated |= path.leaf() || path.is_max_screen_space_error();
    layer->refinement_hysteresis_updated |= path.leaf() || path.is_refinement_hysteresis();
    layer->id_attribute_name_updated |= path.leaf();
    layer->special_attributes_updated |= path.leaf();
    layer->vector_data_layer_id_updated |= path.leaf() || path.is_vector_data_layer_id();
    layer->attributes_updated |=
        path.leaf() || path.is_vector_data_layer_id() || path.is_attributes();
    layer->palettes_updated |= path.leaf() || path.is_palettes();
    layer->rng_seed_updated |= path.leaf() || path.is_rng_seed();
    layer->styling_script_updated |= path.leaf() || path.is_styling_script();
    layer->visibility_updated |= path.leaf() || path.is_visible();
    layer->loading_priority_updated |= path.leaf() || path.is_loading_priority();
    layer->scene_views_updated |= path.leaf() || path.is_scene_views();
    layer->appearance_updated |= path.leaf() || path.is_clip_id() || path.is_lighting()
        || path.is_draw_under_flat_overlays();
    layer->visibility_constraints_updated |= path.leaf() || path.is_visibility_constraints();
    layer->http_headers_updated |= path.leaf() || path.is_http_headers();
    layer->material_properties_updated = path.is_material_properties() || path.leaf();

    if (path.is_material_properties())
    {
        auto prps_path = path.clone().material_properties();
        if (prps_path.leaf() || prps_path.is_base_material() || prps_path.is_overlay_material()
            || prps_path.is_enable_overlay())
        {
            layer->active_materials_updated = true;
        }
    }
    else if (path.leaf())
    {
        layer->active_materials_updated = true;
    }

    if (path.leaf() || path.is_materials())
    {
        layer->materials_array_sync.notify_model_update(update_type, path);
    }
}

RenderRequest work(
    ThreeDTilesLayerSystem* system,
    SceneModel* model,
    const SelectionSystem* selection,
    AssetsLoader* al,
    JobScheduler* js,
    BlobAllocator* ba,
    ImageDecoder* imgdec,
    AttributionRegistry* attributions,
    std::span<const RenderViewInfo> views_info)
{
    assert(system && model && selection && al && js && ba && imgdec);

    RenderRequest render_request;

    _unregister_layers(system, model, al, js);

    for (auto [global_layer_id, _] : system->global_layer_id_to_handle)
    {
        render_request |=
            _update_layer(system, global_layer_id, model, selection, al, attributions);
    }

    render_request |= system->three_d_tiles.work(al, js, ba, imgdec, attributions, views_info);

    return render_request;
}

bool is_working(ThreeDTilesLayerSystem* system)
{
    return system->three_d_tiles.is_working();
}

namespace
{
struct PickingInfo
{
    uint64_t layer_id;
    std::optional<hrz::vector_data::FeatureId> feature_id;

    TilesetH tileset_handle;
    uint32_t tile_index;
    uint32_t batch_id;
};

std::optional<PickingInfo> _get_picking_info(
    ThreeDTilesLayerSystem* system,
    const picking::ObjectReference& ref)
{
    if (ref.system_id != system->system_picking_id) return std::nullopt;

    TilesetH tileset_handle = 0;
    uint32_t tile_index = 0;
    uint32_t batch_id = 0;

    ThreeDTilesSystem::extract_info_from_object_reference(
        ref, &tileset_handle, &tile_index, &batch_id);

    const LayerH layer_handle = system->three_d_tiles.get_layer_for_tileset(tileset_handle);

    if (tileset_handle == 0)
    {
        HRZ_LOG_ERROR("Cannot find 3D Tiles layer for tileset {}", tileset_handle);
        return std::nullopt;
    }

    if (!system->three_d_tiles.is_tileset_visible(tileset_handle))
    {
        return std::nullopt;
    }

    const uint64_t global_layer_id = system->layer_handles_to_global_id.at(layer_handle);
    auto feature_id = system->three_d_tiles.get_feature_id(tileset_handle, tile_index, batch_id);

    return {{global_layer_id, feature_id, tileset_handle, tile_index, batch_id}};
}
} // namespace

void pick(
    ThreeDTilesLayerSystem* system,
    const picking::PositionResult& result_raw,
    hrz_proto::PickResults& picking_results)
{
    assert(system);
    assert(result_raw.position.has_value());

    auto info_opt = _get_picking_info(system, result_raw.ref);

    if (!info_opt.has_value()) return;

    auto& info = info_opt.value();

    auto result = picking_results.add_results();
    result->mutable_layer()->set_type(hrz_proto::LayerType::THREE_D_TILES);
    result->mutable_layer()->mutable_handle()->set_opaque(info.layer_id);
    if (info.feature_id.has_value() && !info.feature_id->is_null())
    {
        info.feature_id->to_proto(result->mutable_three_d_tile()->mutable_feature_id());
    }
    result->mutable_three_d_tile()->mutable_position()->set_x(result_raw.position.value().x);
    result->mutable_three_d_tile()->mutable_position()->set_y(result_raw.position.value().y);
    result->mutable_three_d_tile()->mutable_position()->set_z(result_raw.position.value().z);
    result->mutable_three_d_tile()->set_tile_url(
        system->three_d_tiles.get_url_for_tile(info.tileset_handle, info.tile_index));
    result->mutable_three_d_tile()->set_data_texture_value(result_raw.data_texture_value);

    if (!info.feature_id.has_value() || info.feature_id->is_null())
    {
        return;
    }

    auto attribute_count = system->three_d_tiles.get_attribute_count(info.tileset_handle);
    for (unsigned int i = 0; i < attribute_count; ++i)
    {
        auto value_opt = system->three_d_tiles.get_attribute_value(
            info.tileset_handle, info.tile_index, info.batch_id, i);

        if (!value_opt.has_value()) continue;

        const auto& value = value_opt.value();

        result->mutable_three_d_tile()->add_names(value.name.data(), value.name.size());
        *result->mutable_three_d_tile()->add_values() = value.value;
    }
}

std::pair<size_t, size_t> make_typed_object_references(
    ThreeDTilesLayerSystem* system,
    std::span<const picking::ObjectReference> objs,
    std::span<hrz_proto::TypedObjectReference> output)
{
    assert(objs.size() <= output.size());

    size_t in_cursor = 0;
    size_t out_cursor = 0;

    TilesetH tileset_handle = 0;
    LayerH layer_handle = 0;
    uint64_t layer_id = 0;
    bool is_visible = false;

    while (in_cursor < objs.size())
    {
        const auto& obj = objs[in_cursor];
        if (obj.system_id == system->system_picking_id)
        {
            TilesetH new_tileset_handle = 0;
            uint32_t tile_index = 0;
            uint32_t batch_id = 0;

            ThreeDTilesSystem::extract_info_from_object_reference(
                obj, &new_tileset_handle, &tile_index, &batch_id);

            if (new_tileset_handle != tileset_handle)
            {
                tileset_handle = new_tileset_handle;
                if (tileset_handle != 0)
                {
                    layer_handle = system->three_d_tiles.get_layer_for_tileset(tileset_handle);
                    is_visible = system->three_d_tiles.is_tileset_visible(tileset_handle);
                    layer_id = system->layer_handles_to_global_id.at(layer_handle);
                }
            }

            if (is_visible && tileset_handle != 0)
            {
                auto feature_id =
                    system->three_d_tiles.get_feature_id(tileset_handle, tile_index, batch_id);

                auto ref = output[out_cursor++].mutable_three_d_tiles();
                ref->mutable_layer()->set_opaque(layer_id);
                if (feature_id.has_value() && !feature_id->is_null())
                {
                    feature_id->to_proto(ref->mutable_feature_id());
                }
            }

            in_cursor++;
        }
        else
        {
            // We assume that objs are sorted, thus if we find an object
            // reference without the correct system id, we assume there are no
            // more inputs with the same system id.
            break;
        }
    }

    return std::make_pair(in_cursor, out_cursor);
}

std::optional<picking::FeatureReference> make_feature_reference(
    ThreeDTilesLayerSystem* system,
    const picking::ObjectReference& obj)
{
    assert(system);

    if (obj.system_id != system->system_picking_id)
    {
        return std::nullopt;
    }

    TilesetH tileset_handle = 0;
    uint32_t tile_index = 0;
    uint32_t batch_id = 0;

    ThreeDTilesSystem::extract_info_from_object_reference(
        obj, &tileset_handle, &tile_index, &batch_id);

    if (tileset_handle != 0)
    {
        const LayerH layer_handle = system->three_d_tiles.get_layer_for_tileset(tileset_handle);
        const Layer* layer = system->layer_pool.get_object(layer_handle);

        if (layer)
        {
            auto feature_id =
                system->three_d_tiles.get_feature_id(tileset_handle, tile_index, batch_id);

            if (feature_id.has_value() && !feature_id->is_null())
            {
                picking::FeatureReference ref;
                ref.system_id = system->system_picking_id;
                ref.complementary_id = layer_handle;
                ref.feature_id_hash = feature_id.value().hash();
                return ref;
            }
        }
    }

    return std::nullopt;
}

RenderRequest work_gpu(ThreeDTilesLayerSystem* system, Render* render, BlobAllocator* ba)
{
    assert(system && render && ba);

    return system->three_d_tiles.work_gpu(render, ba);
}

void draw(ThreeDTilesLayerSystem* system, Render* render, AttributionRegistry* attributions)
{
    assert(system && render);

    system->three_d_tiles.draw(render, attributions);
}
} // namespace three_d_tiles_layers
} // namespace hrz
