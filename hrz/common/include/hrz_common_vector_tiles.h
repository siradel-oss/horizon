#pragma once

#include "hrz_common_blob_array.h"
#include "hrz_common_font_rasterizer.h"
#include "hrz_common_picking_types.h"
#include "hrz_common_style.h"
#include "hrz_common_tile_coords.h"
#include "hrz_common_vector_data.h"
#include "hrz_fnd_flat_hash_map.h"

#include <hrz_fnd_inlined_vector.h>
#include <hrz_protocol_all.h>

#include <lin_maths.h>

#include <optional>
#include <vector>

namespace hrz::vt
{
// Picking IDs (object reference):
//
// 2 bits per character, least significant bits are on the right.
//
// rrrrrrrrrrrrrrrr|gggggggggggggggg        (uvec2 components)
// ccccccccccccssss|oooooooooooooooo        (system, complementary, object)
// llllllltttttssss|tttttttiiiiiiiii        (contents)
// layer_id        | object_id
//
// s: system ID, 8 bits
// l: local layer ID, 14 bits
// t: tile ID, 24 bits
// i: feature index, 18 bits (262,144 values)
//
// When the complementary ID is passed to one of the functions below, it must
// have been shifted to the right by the size of the system ID.
// (i.e. hrz::picking::extract_complementary_id() must have been used.)

static constexpr uint32_t MAX_FEATURE_INDEX = (1 << 18) - 1;

inline uint32_t extract_feature_index(const picking::ObjectReference& ref)
{
    return ref.object_id & ((1 << 18) - 1);
}

inline uint32_t extract_tile_id(const picking::ObjectReference& ref)
{
    return ((ref.complementary_id & ((1 << 10) - 1)) << 14) | (ref.object_id >> 18);
}

inline uint32_t extract_local_layer_id(const picking::ObjectReference& ref)
{
    return (ref.complementary_id >> 10) & ((1 << 14) - 1);
}

inline uint32_t make_complementary_id_for_layer_local_id(uint32_t layer_local_id)
{
    assert(layer_local_id <= ((1 << 14) - 1));
    return layer_local_id << 10;
}

inline uint32_t make_system_layer_id_partial(uint8_t system_id, uint32_t layer_local_id)
{
    return picking::make_layer_id(
        system_id, make_complementary_id_for_layer_local_id(layer_local_id));
}

inline picking::ObjectReference make_object_reference(
    uint32_t system_layer_id_partial,
    uint32_t tile_id)
{
    picking::ObjectReference obj_ref{
        picking::extract_system_id_from_layer_id(system_layer_id_partial),
        picking::extract_complementary_id_from_layer_id(system_layer_id_partial),
        0,
    };

    assert(tile_id <= ((1 << 24) - 1));
    obj_ref.complementary_id |= (tile_id >> 14);
    obj_ref.object_id = (tile_id & (1 << 14) - 1) << 18;

    return obj_ref;
}

inline picking::FeatureReference make_feature_reference(uint32_t system_layer_id_partial)
{
    picking::FeatureReference feature_ref;
    feature_ref.system_id = picking::extract_system_id_from_layer_id(system_layer_id_partial);
    feature_ref.complementary_id =
        picking::extract_complementary_id_from_layer_id(system_layer_id_partial);
    return feature_ref;
}

struct ExtrudedVectorData
{
    hrz::TileCoords coords;
    hrz::BlobArray<vector_data::FeatureIdHash> feature_ids;
    vector_data::VectorTileGeometry geometry;
    style::StyledFeatures style;
    hrz_proto::VectorClamping clamping;
    hrz::BlobArray<float> clamps;

    float default_extrusion;
    lm::vec4 default_upper_color;
    lm::vec4 default_lower_color;
    lm::vec4 default_roof_color;
    float default_altitude_offset;

    uint32_t repr_id;
    uint64_t extrusion_prp;
    uint64_t upper_color_prp;
    uint64_t lower_color_prp;
    uint64_t roof_color_prp;
    uint64_t altitude_offset_prp;

    bool clip_to_tile;
};

struct ExtrudedVectorGeometry
{
#pragma pack(push, 4)

    struct Vertex
    {
        lm::vec3 position;
        uint32_t normal; // oct-encoded
        lm::ubvec4 color;

        // Feature indices are the indices into feature_ids. They will also serve as
        // indices into the selection bitmask.
        uint32_t feature_index;
    };

#pragma pack(pop)

    hrz::BlobArray<Vertex> vertex_data;

    hrz::BlobArray<uint32_t> indices;

    // Padded so that it can be uploaded to a 512-pixel-wide texture.
    hrz::BlobArray<vector_data::FeatureIdHash> feature_ids;

    //
    // Everything below this is not part of the geometry.
    //

    uint32_t max_feature_index;

    lm::dvec3 center;
    double bsphere_radius;
    lm::dvec3 bsphere_center;
    bool has_transparency;
};

struct ModelData
{
    hrz::BlobArray<vector_data::FeatureIdHash> feature_ids;
    vector_data::VectorTileGeometry geometry;
    style::StyledFeatures style;
    hrz_proto::VectorClamping clamping;
    hrz::BlobArray<float> clamps;
    hrz::TileCoords tile_coords;
    bool clip_to_tile;

    lm::dmat4 frame;
    hrz_proto::EulerRotationOrder rotation_order;

    lm::vec3 default_scale;
    lm::vec4 default_color;
    lm::vec3 default_world_offset;
    lm::vec3 default_rotation;

    uint32_t repr_id;
    uint64_t scale_x_prp;
    uint64_t scale_y_prp;
    uint64_t scale_z_prp;
    uint64_t color_prp;
    uint64_t world_offset_x_prp;
    uint64_t world_offset_y_prp;
    uint64_t world_offset_z_prp;
    uint64_t rotation_x_prp;
    uint64_t rotation_y_prp;
    uint64_t rotation_z_prp;
};

struct ModelGeometry
{
    std::vector<lm::vec3> positions;
    std::vector<lm::usvec4> normals;
    std::vector<lm::vec3> scales;
    std::vector<lm::ubvec4> colors;
    std::vector<vector_data::FeatureIdHash> feature_ids;
    std::vector<uint32_t> object_ids;

    std::vector<lm::vec3> impostor_positions;
    std::vector<lm::vec3> impostor_scales; // In the Horizon frame
    std::vector<lm::uvec2> impostor_orientations;
    std::vector<lm::vec3> normals_to_ground;
    double average_scale;

    // The position from which model positions are relative to.
    lm::dvec3 origin;

    hrz::BSphere<double> bsphere;
};

struct FlatVectorData
{
    hrz::BlobArray<vector_data::FeatureIdHash> feature_ids;
    hrz::TileCoords coords;
    vector_data::VectorTileGeometry geometry;
    style::StyledFeatures style;

    float default_line_width;
    lm::vec4 default_color;
    float default_disc_radius;
    lm::vec4 default_empty_color;

    uint32_t repr_id;
    uint64_t line_width_prp;
    uint64_t color_prp;
    uint64_t disc_radius_prp;
    uint64_t dash_period_prp;
    uint64_t dash_length_prp;
    uint64_t animation_speed_prp;
    uint64_t empty_color_prp;

    bool clip_to_tile;
    bool polygons_outline;
    float default_dash_period;
    float default_dash_length;
    float default_animation_speed;

    struct Sprite
    {
        lm::uvec2 atlas_size;
        lm::uvec2 atlas_offset;
    };

    lm::uvec2 pattern_texture_size;
    hrz::InlinedVector<Sprite, 8> pattern_sprites;
    hrz::flat_hash_map<std::string, size_t> pattern_sprite_name_to_index;

    bool has_polygon_pattern;

    uint64_t polygon_pattern_sprite_index_prp;
    uint64_t polygon_pattern_sprite_name_prp;
    lm::ulvec2 polygon_pattern_size_prp;
    uint64_t polygon_pattern_rotation_prp;
    uint64_t polygon_pattern_color_prp;
    uint64_t polygon_pattern_color_blend_strength_prp;

    size_t default_polygon_pattern_sprite_index;
    std::string default_polygon_pattern_sprite_name;
    lm::vec2 default_polygon_pattern_size;
    float default_polygon_pattern_rotation;
    lm::vec4 default_polygon_pattern_color;
    float default_polygon_pattern_color_blend_strength;

    hrz_proto::PolygonPatternSizeUnit polygon_pattern_size_unit;
};

struct FlatVectorGeometry
{
#pragma pack(push, 4)

    // This definition must be in sync with the one in the vertex shader code.
    // /!\ Update hash computation and equality function if modified.
    struct PolygonPatternStyle
    {
        lm::vec2 sprite_size;
        lm::vec2 sprite_offset;
        lm::mat2 polygon_pattern_transform;
        lm::ubvec4 background_color;
        lm::ubvec4 pattern_color;
        float pattern_color_blend_strength;
        uint32_t _padding;
    };

#pragma pack(pop)

    static_assert(
        sizeof(PolygonPatternStyle) % sizeof(lm::uvec4) == 0,
        "Pattern polygon style data must fit in an RGBA32UI texture");

    hrz::BlobArray<PolygonPatternStyle> polygon_pattern_style_data;

#pragma pack(push, 4)

    struct SolidColorPolygonVertex
    {
        lm::vec3 position;
        lm::ubvec4 color;
        uint32_t feature_index;
    };

    struct PatternPolygonVertex
    {
        lm::vec3 position;
        lm::vec2 uv;
        float in_tile_lat;
        uint32_t pattern_style_index;
        uint32_t feature_index;
    };

#pragma pack(pop)

    std::variant<hrz::BlobArray<SolidColorPolygonVertex>, hrz::BlobArray<PatternPolygonVertex>>
        polygon_data;
    hrz::BlobArray<uint32_t> polygon_indices;

#pragma pack(push, 4)

    struct PolylineInstance
    {
        lm::vec3 position0;
        lm::vec3 position1;
        uint32_t normal0; // oct-encoded
        uint32_t normal1; // oct-encoded
        lm::ubvec4 color;
        float line_width;
        float line_total_length;
        float progress_at_start;
        float progress_at_end;
        float dash_period;
        float dash_length;
        float animation_speed;
        lm::ubvec4 empty_color;
        uint32_t feature_index;
    };

#pragma pack(pop)

    hrz::BlobArray<PolylineInstance> polyline_data;

#pragma pack(push, 4)

    struct PointInstance
    {
        lm::vec3 position;
        lm::ubvec4 color;
        float disc_radius;
        uint32_t feature_index;
    };

#pragma pack(pop)

    hrz::BlobArray<PointInstance> point_data;

    // Padded so that it can be uploaded to a 512-pixel-wide texture.
    hrz::BlobArray<vector_data::FeatureIdHash> feature_ids;

    uint32_t max_feature_index;

    lm::dbbox2 wmerc_bounds;
    lm::dvec3 sea_bsphere_center;
    double sea_bsphere_radius;

    bool is_animated;

    lm::dvec2 origin_uv;
    double origin_lat;
    double lat_span;
};

struct HeatmapData
{
    hrz::TileCoords coords;
    vector_data::VectorTileGeometry geometry;
    style::StyledFeatures style;

    float default_value;
    float default_disc_radius;

    uint32_t repr_id;
    uint64_t value_prp;
    uint64_t disc_radius_prp;
};

struct HeatmapGeometry
{
#pragma pack(push, 4)

    struct PointInstance
    {
        lm::vec3 position;
        float value;
        float disc_radius;
    };

#pragma pack(pop)

    hrz::BlobArray<PointInstance> point_data;

    hrz::BSphere<double> bsphere;
};

struct CylinderVectorData
{
    hrz::BlobArray<vector_data::FeatureIdHash> feature_ids;
    vector_data::VectorTileGeometry geometry;
    style::StyledFeatures style;
    hrz_proto::VectorClamping clamping;
    hrz::BlobArray<float> clamps;

    lm::vec4 default_color;
    lm::vec4 default_empty_color;
    float default_radius;
    float default_altitude_offset;
    float default_dash_period;
    float default_dash_length;
    float default_animation_speed;

    uint32_t repr_id;
    uint64_t color_prp;
    uint64_t empty_color_prp;
    uint64_t radius_prp;
    uint64_t altitude_offset_prp;
    uint64_t dash_period_prp;
    uint64_t dash_length_prp;
    uint64_t animation_speed_prp;

    hrz_proto::DashMode dash_mode;
};

struct CylinderVectorGeometry
{
#pragma pack(push, 4)

    struct Instance
    {
        lm::vec3 position0;
        uint32_t normal0; // oct-encoded
        lm::vec3 position1;
        uint32_t normal1; // oct-encoded
        lm::ubvec4 color;
        lm::vec2 radii;
        float line_total_length;
        float progress0;
        float progress1;
        float dash_period;
        float dash_length;
        float animation_speed;
        lm::ubvec4 alternative_color;
        uint32_t feature_index;
        vector_data::FeatureIdHash feature_id;
    };

#pragma pack(pop)

    hrz::BlobArray<Instance> instance_data;

    lm::dvec3 center;
    double bsphere_radius;
    lm::dvec3 bsphere_center;
    bool has_transparency;
    bool is_animated;
};

static constexpr uint32_t AnchorFlag_Unit_Meters = 0;
static constexpr uint32_t AnchorFlag_Unit_Pixels = 1;
static constexpr uint32_t AnchorFlag_Unit_PixelsRelativeToAnchorDistance = 2;
static constexpr uint32_t AnchorFlag_Unit_PixelsRelativeToCameraHeight = 3;

static constexpr uint32_t AnchorFlag_PosOffsetUnitMask = 0x0000'0003;
static constexpr uint32_t AnchorFlag_PosOffsetUnitShift = 0;

static constexpr uint32_t AnchorFlag_ElementSizeUnitMask = 0x0000'000c;
static constexpr uint32_t AnchorFlag_ElementSizeUnitShift = 2;

// Keep this in sync with the GLSL code
static constexpr uint32_t AnchorFlag_AlignXAxisToScreen = 0x0000'0010;
static constexpr uint32_t AnchorFlag_AlignYAxisToScreen = 0x0000'0020;
static constexpr uint32_t AnchorFlag_KeepUpright = 0x0000'0040;

// These flags are not used on the GPU
static constexpr uint32_t AnchorFlag_CanOverlapOtherSymbols = 0x80000000;
static constexpr uint32_t AnchorFlag_HidesOtherSymbols = 0x40000000;
static constexpr uint32_t AnchorFlag_IsOptional = 0x20000000;

struct AnchorPrototype
{
    // See above.
    uint32_t flags;

    float reference_distance;
    float min_relative_scale;
    float max_relative_scale;
};

struct SymbolBakingData
{
    struct Placeholder
    {
        lm::vec2 default_size;
        lm::ulvec2 size_prp;
        lm::ubvec4 default_color;
        uint64_t color_prp;
    };

    struct LeaderLine
    {
        float width;

        lm::vec3 default_target_offset;
        lm::ulvec3 target_offset_prp;

        lm::ubvec4 default_color;
        uint64_t color_prp;
    };

    struct Anchor
    {
        lm::vec3 default_position_offset;
        lm::ulvec3 position_offset_prp;

        lm::vec3 default_rotation;
        lm::ulvec3 rotation_prp;
        hrz_proto::EulerRotationOrder rotation_order;

        lm::vec2 default_element_alignment;
        lm::ulvec2 element_alignment_prp;

        float default_culling_priority;
        uint64_t culling_priority_prp;

        bool reset_layout;

        uint32_t child_index;
    };

    struct Stack
    {
        lm::vec2 default_alignment;
        lm::ulvec2 alignment_prp;
        hrz::InlinedVector<uint32_t, 8> child_indices;
    };

    struct StackExpand
    {
        uint32_t child_index;
    };

    struct Padding
    {
        float default_left_padding;
        float default_right_padding;
        float default_top_padding;
        float default_bottom_padding;

        uint64_t left_padding_prp;
        uint64_t right_padding_prp;
        uint64_t top_padding_prp;
        uint64_t bottom_padding_prp;

        uint32_t child_index;
    };

    struct SizedBox
    {
        lm::vec2 default_size;

        lm::ulvec2 size_prp;

        uint32_t child_index;
    };

    struct Flex
    {
        hrz_proto::FlexAxis main_axis;
        hrz_proto::FlexMainAxisAlignment main_axis_alignment;
        hrz_proto::FlexCrossAxisAlignment cross_axis_alignment;

        hrz::InlinedVector<uint32_t, 8> child_indices;
    };

    struct Flexible
    {
        hrz_proto::FlexFit fit;
        float factor;

        uint32_t child_index;
    };

    struct Image
    {
        struct Sprite
        {
            lm::ivec2 atlas_offset;
            lm::ivec2 atlas_size;

            lm::ivec2 content_offset_fixed;
            lm::ivec2 content_offset_stretch;

            lm::ivec2 content_size_fixed;
            lm::ivec2 content_size_stretch;

            lm::ivec2 full_size_fixed;
            lm::ivec2 full_size_stretch;

            int geometry_index;
        };

        struct SpriteGeometry
        {
            uint32_t first_index;
            uint32_t index_count;
        };

        hrz::flat_hash_map<std::string, int> sprite_name_to_index;

        hrz_proto::BoxFitAxes fit_axes;
        hrz_proto::BoxFit fit_mode;
        lm::ivec2 image_size;
        lm::ubvec4 default_color;
        float default_scale;
        int default_sprite_index;
        std::string default_sprite_name;
        lm::vec2 default_alignment;
        uint64_t color_prp;
        uint64_t scale_prp;
        uint64_t sprite_index_prp;
        uint64_t sprite_name_prp;
        lm::ulvec2 alignment_prp;

        hrz::InlinedVector<SpriteGeometry, 4> sprite_geometries;
        hrz::InlinedVector<Sprite, 8> sprites;
    };

    struct ConstrainedBox
    {
        lm::vec2 default_min_size;
        lm::vec2 default_max_size;
        lm::ulvec2 min_size_prp;
        lm::ulvec2 max_size_prp;
        uint32_t child_index;
    };

    struct RotatedBox
    {
        int64_t default_quarter_turns;
        uint64_t quarter_turns_prp;
        uint32_t child_index;
    };

    struct AspectRatio
    {
        float default_aspect_ratio;
        uint64_t aspect_ratio_prp;
        uint32_t child_index;
    };

    struct DecoratedShape
    {
        hrz_proto::DecoratedShapeType shape_type;

        union
        {
            struct
            {
                int sides;
                float star;
            } regular_polygon;
        } shape_params;

        lm::vec2 alignment;
        hrz_proto::BoxFit fit_mode;
        float aspect_ratio;

        lm::ubvec4 default_color;
        lm::ubvec4 default_border_color;
        float default_border_size;
        float default_border_radius;

        uint64_t color_prp;
        uint64_t border_color_prp;
        uint64_t border_size_prp;
        uint64_t border_radius_prp;
    };

    struct FittedBox
    {
        hrz_proto::BoxFit fit;
        lm::vec2 default_alignment;

        lm::ulvec2 alignment_prp;

        uint32_t child_index;
    };

    struct Transform
    {
        struct Translation
        {
            lm::vec3 default_translation;
            lm::ulvec3 translation_prp;
        };

        struct Scaling
        {
            lm::vec3 default_scaling;
            lm::ulvec3 scaling_prp;
        };

        struct Rotation
        {
            hrz_proto::EulerRotationOrder order;
            lm::vec3 default_rotation;
            lm::ulvec3 rotation_prp;
        };

        struct Generic
        {
            lm::mat4 matrix;
        };

        using Component = std::variant<Translation, Scaling, Rotation, Generic>;

        lm::vec2 default_origin;
        lm::vec2 default_alignment;

        lm::ulvec2 origin_prp;
        lm::ulvec2 alignment_prp;

        hrz::InlinedVector<Component, 4> components;

        uint32_t child_index;
    };

    struct Text
    {
        std::string default_text;
        uint64_t text_prp;
        float default_font_size;
        uint64_t font_size_prp;
        lm::ubvec4 default_fill_color;
        uint64_t fill_color_prp;
        float default_outline_size;
        uint64_t outline_size_prp;
        hrz_proto::TextOutlineWidthUnit outline_size_unit;
        lm::ubvec4 default_outline_color;
        uint64_t outline_color_prp;
        hrz_proto::TextAlignment default_alignment;
        uint64_t alignment_prp;
        float default_line_spacing;
        uint64_t line_spacing_prp;
        font_rasterizer::FontHandle font;
    };

    struct Optional
    {
        bool default_display_child;
        uint64_t display_child_prp;
        uint32_t child_index;
    };

    struct Variant
    {
        int64_t default_displayed_child_index;
        uint64_t displayed_child_index_prp;
        hrz::InlinedVector<uint32_t, 8> child_indices;
    };

    using ElementBakingParams = std::variant<
        Placeholder,
        Anchor,
        LeaderLine,
        Stack,
        Image,
        Padding,
        SizedBox,
        Flex,
        Flexible,
        StackExpand,
        ConstrainedBox,
        RotatedBox,
        DecoratedShape,
        AspectRatio,
        FittedBox,
        Transform,
        Text,
        Optional,
        Variant>;

    struct Element
    {
        hrz_proto::SymbolElementType type;
        std::optional<uint32_t> parent_index; // Index among all elements
        std::optional<uint32_t> anchor_index; // Index among anchors
        std::optional<uint32_t> z_index;
        ElementBakingParams params;

        const Placeholder& placeholder() const { return std::get<Placeholder>(params); }

        const Anchor& anchor() const { return std::get<Anchor>(params); }

        const LeaderLine& leader_line() const { return std::get<LeaderLine>(params); }

        const Stack& stack() const { return std::get<Stack>(params); }

        const Image& image() const { return std::get<Image>(params); }

        const Padding& padding() const { return std::get<Padding>(params); }

        const SizedBox& sized_box() const { return std::get<SizedBox>(params); }

        const Flex& flex() const { return std::get<Flex>(params); }

        const Flexible& flexible() const { return std::get<Flexible>(params); }

        const StackExpand& stack_expand() const { return std::get<StackExpand>(params); }

        const ConstrainedBox& constrained_box() const { return std::get<ConstrainedBox>(params); }

        const RotatedBox& rotated_box() const { return std::get<RotatedBox>(params); }

        const DecoratedShape& decorated_shape() const { return std::get<DecoratedShape>(params); }

        const AspectRatio& aspect_ratio() const { return std::get<AspectRatio>(params); }

        const FittedBox& fitted_box() const { return std::get<FittedBox>(params); }

        const Transform& transform() const { return std::get<Transform>(params); }

        const Text& text() const { return std::get<Text>(params); }

        const Optional& optional() const { return std::get<Optional>(params); }

        const Variant& variant() const { return std::get<Variant>(params); }
    };

    hrz::TileCoords tile_coords;
    uint32_t repr_id;
    hrz::BlobArray<vector_data::FeatureIdHash> feature_ids;
    vector_data::VectorTileGeometry geometry;
    style::StyledFeatures style;
    hrz_proto::VectorClamping clamping;
    hrz::BlobArray<float> clamps;

    bool clip_to_tile;

    hrz::InlinedVector<Element, 16> elements;
};

struct BakedSymbols
{
#pragma pack(push, 4)

    struct PlaceholderInstance
    {
        lm::mat4 transform;
        lm::vec2 size;
        lm::ubvec4 color;
        int32_t anchor_index;
    };

    struct ImageInstance
    {
        lm::mat4 transform;
        lm::vec2 stretch_size;
        lm::ubvec4 color;
        int32_t anchor_index;
        lm::vec2 uv_offset;
        lm::vec2 uv_size;
    };

    struct DecoratedShapeInstance
    {
        lm::mat4 transform;
        lm::vec2 size;
        lm::ubvec4 color;
        lm::ubvec4 border_color;
        lm::vec2 border_size_radius;
        int32_t anchor_index;
    };

    struct LeaderLineInstance
    {
        lm::vec3 target_in_tile_position; // In ECEF
        lm::vec3 in_symbol_position;      // In symbol coordinates
        lm::ubvec4 color;
        int32_t anchor_index;
    };

#pragma pack(pop)

    struct ImageInstances
    {
        struct Batch
        {
            uint32_t first_index{};
            uint32_t index_count{};
            uint32_t first_instance{};
            uint32_t instance_count{};
        };

        hrz::BlobArray<ImageInstance> instances;
        std::vector<Batch> batches;
    };

    struct TextInstances
    {
#pragma pack(push, 4)

        struct GlyphPositionUv
        {
            lm::vec2 xy0;
            lm::vec2 uv0;
            lm::vec2 xy1;
            lm::vec2 uv1;
            lm::vec2 xy2;
            lm::vec2 uv2;
            lm::vec2 xy3;
            lm::vec2 uv3;
        };

#pragma pack(pop)

        hrz::BlobArray<lm::mat4> transforms;           // By text
        hrz::BlobArray<uint32_t> anchor_indices;       // By text
        hrz::BlobArray<float> outline_widths;          // By text
        hrz::BlobArray<lm::ubvec4> fill_colors;        // By text
        hrz::BlobArray<lm::ubvec4> outline_colors;     // By text
        hrz::BlobArray<GlyphPositionUv> positions_uvs; // By glyph
        hrz::BlobArray<uint16_t> text_indices;         // By glyph

        // True if at least one text has a non-zero outline width.
        bool has_non_zero_outline_width;
    };

    struct ElementInstances
    {
        hrz_proto::SymbolElementType type;
        std::variant<
            hrz::BlobArray<PlaceholderInstance>,
            hrz::BlobArray<ImageInstance>,
            ImageInstances,
            hrz::BlobArray<DecoratedShapeInstance>,
            hrz::BlobArray<LeaderLineInstance>,
            TextInstances>
            data;
    };

#pragma pack(push, 4)

    // This definition must be in sync with the one in the vertex shader code.
    struct AnchorGpu
    {
        lm::vec3 in_tile_position;
        uint32_t _padding_0;
        lm::vec3 position_offset; // In ECEF
        uint32_t _padding_1;
        lm::vec3 rotation;        // Euler angles, XYZ order
        uint32_t local_east_axis; // oct-encoded
        uint32_t local_up_axis;   // oct-encoded
        uint32_t feature_index;
        vector_data::FeatureIdHash feature_id;
    };

#pragma pack(pop)

    static_assert(
        sizeof(AnchorGpu) % sizeof(lm::uvec4) == 0,
        "Anchor data must fit in an RGBA32UI texture");

    struct AnchorCulling
    {
        lm::bbox2 rect;

        lm::vec3 in_tile_position;
        lm::vec3 position_offset;

        lm::vec3 rotation; // XYZ euler angles

        lm::vec3 local_east_axis;
        lm::vec3 local_up_axis;

        float priority;

        uint32_t anchor_prototype_index;
    };

    // For each symbol instance, this is the span of anchor in the anchors arrays.
    // This is used when we want to access all anchors of a single symbol, for
    // example when culling.
    struct AnchorSpan
    {
        uint32_t first_anchor;
        uint32_t anchor_count;
    };

    hrz::BlobArray<AnchorGpu> anchor_gpu_data;
    hrz::BlobArray<AnchorCulling> anchor_culling_data;
    hrz::BlobArray<AnchorSpan> anchor_spans;

    // Ordered by z-index
    std::vector<std::optional<ElementInstances>> instances;

    uint32_t max_feature_index;

    // ECEF coordinates
    lm::dvec3 tile_center;
    double bsphere_radius;
    lm::dvec3 bsphere_center;
};

struct SymbolCullingParams
{
    struct ViewInfo
    {
        hrz_proto::SceneViewIndex view_index;

        lm::dvec3 position;
        lm::mat4 view_cc;
        lm::mat4 proj;
        lm::mat4 inv_view;

        float pixel_size_in_meters;
        lm::vec2 small_size;

        float perceived_distance;
    };

    struct Group
    {
        uint64_t handle;
        uint32_t visible_in_views;

        lm::dvec3 origin;
        uint16_t z_index;

        hrz::InlinedVector<vt::AnchorPrototype, 8> anchor_protos;

        hrz::BlobArray<vt::BakedSymbols::AnchorSpan> anchor_spans;
        hrz::BlobArray<vt::BakedSymbols::AnchorCulling> anchors;
    };

    uint64_t frame;
    std::vector<Group> groups;
    hrz::InlinedVector<ViewInfo, hrz_proto::SceneViewIndex_ARRAYSIZE> views;
};

struct SymbolCullingResponse
{
    static constexpr uint32_t BITSET_TEXTURE_WIDTH = 1024;

    struct GroupId
    {
        uint64_t handle;
        hrz_proto::SceneViewIndex scene_view;
    };

    struct Group : public GroupId
    {
        uint32_t first_bitset_bucket;
        uint32_t bitset_bucket_count;
        lm::ubbox2 ones_bbox;
    };

    uint64_t frame;
    std::vector<GroupId> groups_to_reset;
    std::vector<Group> groups_to_update;
    std::array<hrz::BlobArray<uint32_t>, hrz_proto::SceneViewIndex_ARRAYSIZE> view_bitsets;
};

static constexpr uint32_t DATA_TEXTURE_SIZE = HRZ_S_VECTOR_REPR_DATA_TEXTURE_WIDTH;

inline lm::uvec2 compute_data_texture_size(uint32_t entry_count)
{
    if (entry_count == 0) return {0, 0};

    return {
        std::min(entry_count, DATA_TEXTURE_SIZE),
        std::max((entry_count - 1) / DATA_TEXTURE_SIZE + 1, (uint32_t)1)};
}
} // namespace hrz::vt
