// SPDX-FileCopyrightText: Copyright 2022 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/common/blob_array.h"
#include "hrz/common/font_rasterizer.h"
#include "hrz/common/maths.h"
#include "hrz/common/style/styled_features.h"
#include "hrz/common/tile_coords.h"
#include "hrz/common/vector_data/feature_id_hash.h"
#include "hrz/common/vector_data/tile_geometry.h"
#include "hrz/common/vector_tiles/symbol_anchor_data.h"
#include "hrz/fnd/flat_hash_map.h"
#include "hrz/fnd/inlined_vector.h"
#include "hrz/protocol/maths/geometry.pb.h"
#include "hrz/protocol/scene/index.pb.h"
#include "hrz/protocol/vector/clamping.pb.h"
#include "hrz/protocol/vector/dash.pb.h"
#include "hrz/protocol/vector/polygon_pattern.pb.h"
#include "hrz/protocol/vector/symbol_repr.pb.h"

#include <lin_maths.h>

#include <optional>
#include <vector>

namespace hrz_jobs
{

struct ExtrudedVectorData
{
    hrz::TileCoords coords;
    hrz::BlobArray<hrz::vector_data::FeatureIdHash> feature_ids;
    hrz::vector_data::VectorTileGeometry geometry;
    hrz::style::StyledFeatures style;
    hrz_proto::VectorClamping clamping;
    hrz::BlobArray<float> clamps;

    float default_extrusion;
    lm::ubvec4 default_upper_color_srgb;
    lm::ubvec4 default_lower_color_srgb;
    lm::ubvec4 default_roof_color_srgb;
    float default_altitude_offset;

    uint32_t repr_id;
    uint64_t extrusion_prp;
    uint64_t upper_color_prp;
    uint64_t lower_color_prp;
    uint64_t roof_color_prp;
    uint64_t altitude_offset_prp;

    bool clip_to_tile;
    float bevel_width;
};

struct ExtrudedVectorGeometry
{
#pragma pack(push, 4)

    struct Vertex
    {
        lm::vec3 position;
        uint32_t normal;  // oct-encoded
        lm::ubvec4 color; // sRGB

        // Feature indices are the indices into feature_ids. They will also serve as
        // indices into the selection bitmask.
        uint32_t feature_index;
    };

#pragma pack(pop)

    hrz::BlobArray<Vertex> vertex_data;

    hrz::BlobArray<uint32_t> indices;

    // Padded so that it can be uploaded to a 512-pixel-wide texture.
    hrz::BlobArray<hrz::vector_data::FeatureIdHash> feature_ids;

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
    hrz::BlobArray<hrz::vector_data::FeatureIdHash> feature_ids;
    hrz::vector_data::VectorTileGeometry geometry;
    hrz::style::StyledFeatures style;
    hrz_proto::VectorClamping clamping;
    hrz::BlobArray<float> clamps;
    hrz::TileCoords tile_coords;
    bool clip_to_tile;

    lm::dmat4 frame;
    hrz_proto::EulerRotationOrder rotation_order;

    lm::vec3 default_scale;
    lm::ubvec4 default_color_srgb;
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
    std::vector<lm::ubvec4> colors; // sRGB
    std::vector<hrz::vector_data::FeatureIdHash> feature_ids;
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

struct DashesStyleData
{
    hrz_proto::DashMode mode;

    lm::ubvec4 default_secondary_color_srgb;
    float default_period;
    float default_primary_length;
    float default_animation_speed;

    uint64_t period_prp;
    uint64_t primary_length_prp;
    uint64_t animation_speed_prp;
    uint64_t secondary_color_prp;
};

struct BaseFlatVectorBakingData
{
    hrz::TileCoords coords;
    hrz::BlobArray<hrz::vector_data::FeatureIdHash> feature_ids;
    hrz::vector_data::VectorTileGeometry geometry;
    hrz::style::StyledFeatures style;
    uint32_t repr_id;
};

struct FlatPointData : public BaseFlatVectorBakingData
{
    lm::ubvec4 default_color_srgb;
    uint64_t color_prp;

    float default_radius;
    uint64_t radius_prp;

    lm::ubvec4 default_outline_color_srgb;
    uint64_t outline_color_prp;

    float default_outline_width;
    uint64_t outline_width_prp;

    bool clip_to_tile;
};

struct FlatPolylineData : public BaseFlatVectorBakingData
{
    float default_line_width;
    lm::ubvec4 default_color_srgb;

    uint64_t line_width_prp;
    uint64_t color_prp;

    DashesStyleData dashes;

    bool clip_to_tile;
};

struct FlatPolygonData : public BaseFlatVectorBakingData
{
    lm::ubvec4 default_color_srgb;

    uint64_t color_prp;

    bool clip_to_tile;

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
    lm::ubvec4 default_polygon_pattern_color_srgb;
    float default_polygon_pattern_color_blend_strength;

    hrz_proto::PolygonPatternSizeUnit polygon_pattern_size_unit;
};

struct BaseFlatVectorBakedGeometry
{
    // Padded so that it can be uploaded to a 512-pixel-wide texture.
    hrz::BlobArray<hrz::vector_data::FeatureIdHash> feature_ids;

    uint32_t max_feature_index;

    lm::dbbox2 wmerc_bounds;
    lm::dvec3 sea_bsphere_center;
    double sea_bsphere_radius;
};

struct FlatPointGeometry : public BaseFlatVectorBakedGeometry
{
#pragma pack(push, 4)

    struct PointInstance
    {
        lm::vec3 position;
        lm::ubvec4 color; // sRGB
        float disc_radius;
        lm::ubvec4 outline_color; // sRGB
        float outline_width;
        uint32_t feature_index;
    };

#pragma pack(pop)

    hrz::BlobArray<PointInstance> point_data;
};

struct FlatPolylineGeometry : public BaseFlatVectorBakedGeometry
{
#pragma pack(push, 4)

    struct PolylineInstance
    {
        lm::vec3 position0;
        lm::vec3 position1;
        uint32_t normal0; // oct-encoded
        uint32_t normal1; // oct-encoded
        lm::ubvec4 color; // sRGB
        float line_width;
        float line_total_length;
        float progress_at_start;
        float progress_at_end;
        float dash_period;
        float dash_primary_length;
        float animation_speed;
        lm::ubvec4 secondary_color; // sRGB
        uint32_t feature_index;
    };

#pragma pack(pop)

    hrz::BlobArray<PolylineInstance> polyline_data;

    bool is_animated;
};

struct FlatPolygonGeometry : public BaseFlatVectorBakedGeometry
{
#pragma pack(push, 4)

    // This definition must be in sync with the one in the vertex shader code
    // (in function `fetch_pattern_style`).
    // /!\ Update hash computation and equality function if modified.
    struct PolygonPatternStyle
    {
        lm::vec2 sprite_size;
        lm::vec2 sprite_offset;
        lm::mat2 polygon_pattern_transform;
        lm::ubvec4 background_color_srgb;
        lm::ubvec4 pattern_color_srgb;
        float pattern_color_blend_strength;
        uint32_t _padding;

        constexpr bool operator ==(const PolygonPatternStyle& other) const
        {
            return sprite_size == other.sprite_size && sprite_offset == other.sprite_offset
                && polygon_pattern_transform == other.polygon_pattern_transform
                && background_color_srgb == other.background_color_srgb
                && pattern_color_srgb == other.pattern_color_srgb
                && pattern_color_blend_strength == other.pattern_color_blend_strength;
        }

        template<typename H>
        friend H AbslHashValue(H h, const PolygonPatternStyle& style)
        {
            return H::combine(
                std::move(h), style.sprite_size, style.sprite_offset,
                style.polygon_pattern_transform, style.background_color_srgb,
                style.pattern_color_srgb, style.pattern_color_blend_strength);
        }
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
        lm::ubvec4 color; // sRGB
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

    lm::dvec2 origin_uv;
    double origin_lat;
    double lat_span;
};

struct HeatmapData
{
    hrz::TileCoords coords;
    hrz::vector_data::VectorTileGeometry geometry;
    hrz::style::StyledFeatures style;

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
    hrz::BlobArray<hrz::vector_data::FeatureIdHash> feature_ids;
    hrz::vector_data::VectorTileGeometry geometry;
    hrz::style::StyledFeatures style;
    hrz_proto::VectorClamping clamping;
    hrz::BlobArray<float> clamps;

    lm::ubvec4 default_color_srgb;
    float default_radius;
    float default_altitude_offset;

    uint32_t repr_id;
    uint64_t color_prp;
    uint64_t radius_prp;
    uint64_t altitude_offset_prp;

    DashesStyleData dashes;
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
        lm::ubvec4 color; // sRGB
        lm::vec2 radii;
        float line_total_length;
        float progress0;
        float progress1;
        float dash_period;
        float dash_length;
        float animation_speed;
        lm::ubvec4 alternative_color; // sRGB
        uint32_t feature_index;
        hrz::vector_data::FeatureIdHash feature_id;
    };

#pragma pack(pop)

    hrz::BlobArray<Instance> instance_data;

    lm::dvec3 center;
    double bsphere_radius;
    lm::dvec3 bsphere_center;
    bool has_transparency;
    bool is_animated;
};

struct SymbolBakingData
{
    struct Placeholder
    {
        lm::vec2 default_size;
        lm::ulvec2 size_prp;
        lm::ubvec4 default_color_srgb;
        uint64_t color_prp;
    };

    struct LeaderLine
    {
        float width;

        lm::vec3 default_target_offset;
        lm::ulvec3 target_offset_prp;

        lm::ubvec4 default_color_srgb;
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
        lm::ubvec4 default_color_srgb;
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

        lm::ubvec4 default_color_srgb;
        lm::ubvec4 default_border_color_srgb;
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
        lm::ubvec4 default_fill_color_srgb;
        uint64_t fill_color_prp;
        float default_outline_size;
        uint64_t outline_size_prp;
        hrz_proto::TextOutlineWidthUnit outline_size_unit;
        lm::ubvec4 default_outline_color_srgb;
        uint64_t outline_color_prp;
        hrz_proto::TextAlignment default_alignment;
        uint64_t alignment_prp;
        float default_line_spacing;
        uint64_t line_spacing_prp;
        hrz::font_rasterizer::FontHandle font;
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
        Variant
    >;

    struct Element
    {
        hrz_proto::SymbolElement::ElementTypeCase type;
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
    hrz::BlobArray<hrz::vector_data::FeatureIdHash> feature_ids;
    hrz::vector_data::VectorTileGeometry geometry;
    hrz::style::StyledFeatures style;
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
        lm::ubvec4 color; // sRGB
        int32_t anchor_index;
    };

    struct ImageInstance
    {
        lm::mat4 transform;
        lm::vec2 stretch_size;
        lm::ubvec4 color; // sRGB
        int32_t anchor_index;
        lm::vec2 uv_offset;
        lm::vec2 uv_size;
    };

    struct DecoratedShapeInstance
    {
        lm::mat4 transform;
        lm::vec2 size;
        lm::ubvec4 color;        // sRGB
        lm::ubvec4 border_color; // sRGB
        lm::vec2 border_size_radius;
        int32_t anchor_index;
    };

    struct LeaderLineInstance
    {
        lm::vec3 target_in_tile_position; // In ECEF
        lm::vec3 in_symbol_position;      // In symbol coordinates
        lm::ubvec4 color;                 // sRGB
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
        hrz::BlobArray<lm::ubvec4> fill_colors;        // By text, sRGB
        hrz::BlobArray<lm::ubvec4> outline_colors;     // By text, sRGB
        hrz::BlobArray<GlyphPositionUv> positions_uvs; // By glyph
        hrz::BlobArray<uint16_t> text_indices;         // By glyph

        // True if at least one text has a non-zero outline width.
        bool has_non_zero_outline_width;
    };

    struct ElementInstances
    {
        hrz_proto::SymbolElement::ElementTypeCase type;
        std::variant<
            hrz::BlobArray<PlaceholderInstance>,
            hrz::BlobArray<ImageInstance>,
            ImageInstances,
            hrz::BlobArray<DecoratedShapeInstance>,
            hrz::BlobArray<LeaderLineInstance>,
            TextInstances
        >
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
        hrz::vector_data::FeatureIdHash feature_id;
    };

#pragma pack(pop)

    static_assert(
        sizeof(AnchorGpu) % sizeof(lm::uvec4) == 0,
        "Anchor data must fit in an RGBA32UI texture");

    hrz::BlobArray<AnchorGpu> anchor_gpu_data;
    hrz::BlobArray<hrz::vt::AnchorCullingInfo> anchor_culling_data;

    // For each symbol instance, this is the span of anchor in the anchors arrays.
    // This is used when we want to access all anchors of a single symbol, for
    // example when culling.
    hrz::BlobArray<hrz::vt::AnchorSpan> anchor_spans;

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

        // Size of a pixel in front of the camera
        // at a distance of 1m.
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

        hrz::InlinedVector<hrz::vt::AnchorPrototype, 8> anchor_protos;

        hrz::BlobArray<hrz::vt::AnchorSpan> anchor_spans;
        hrz::BlobArray<hrz::vt::AnchorCullingInfo> anchors;
    };

    uint64_t frame;

    // Maximum allowed relative ground displacement distance when an anchor is moved one
    // pixel vertically on screen. Anchors exceeding this value are culled. This is for
    // horizon de-cluttering.
    // A value of zero disables horizon de-cluttering.
    float max_relative_ground_step;

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

} // namespace hrz_jobs
