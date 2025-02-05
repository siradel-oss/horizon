#pragma once

#include "hrz_core_render.h"

#include <hrz_common_blob_array.h>
#include <hrz_common_maths.h>
#include <hrz_common_monitoring_defs.h>
#include <hrz_fnd_class.h>
#include <hrz_fnd_flat_hash_set.h>

namespace hrz
{

// This class renders a (mostly) static point clouds.
// The point cloud can have batches, each with a feature ID and color that comes from styling.
class PointCloud
{
public:
    struct UniformData
    {
        GlslStd140Mat3 linear_transform;
        GlslStd140Mat3 normal_matrix;
        lm::vec3 rtc_low;
        hrz::bool32 receive_shadows;
        lm::vec3 rtc_high;
        hrz::bool32 lighting_enabled;
        lm::vec3 quantized_position_scale;
        int32_t clip_id;
        lm::vec3 quantized_position_offset;
        uint32_t layer_picking_id;
        lm::uvec2 batch_picking_id;
        uint32_t batch_id_offset;
        uint32_t feature_color_blend_mode;
        float feature_color_blend_strength;
        uint32_t _padding[3];

        bool operator!=(const UniformData& other) const
        {
            return linear_transform != other.linear_transform
                || normal_matrix != other.normal_matrix || rtc_low != other.rtc_low
                || receive_shadows != other.receive_shadows || rtc_high != other.rtc_high
                || lighting_enabled != other.lighting_enabled
                || quantized_position_scale != other.quantized_position_scale
                || clip_id != other.clip_id
                || quantized_position_offset != other.quantized_position_offset
                || layer_picking_id != other.layer_picking_id
                || batch_picking_id != other.batch_picking_id
                || batch_id_offset != other.batch_id_offset
                || feature_color_blend_mode != other.feature_color_blend_mode
                || feature_color_blend_strength != other.feature_color_blend_strength;
        }
    };

    HRZ_CHECK_UBO_SIZE(UniformData);

    struct Geometry
    {
        uint64_t point_count = 0;
        uint32_t batch_count = 0;

        lm::dvec3 quantized_volume_offset;
        lm::dvec3 quantized_volume_scale;
        my::VertexFormat positions_format{};
        hrz::blobs::BlobHandle positions;

        bool has_transparent_color = false;
        my::VertexRate colors_rate{};
        my::VertexFormat colors_format{};
        std::variant<hrz::blobs::BlobHandle, lm::ubvec4> colors;

        my::VertexRate compressed_normals_rate{};
        std::variant<hrz::blobs::BlobHandle, uint16_t> compressed_normals;

        my::VertexRate batch_ids_rate{};
        my::VertexFormat batch_ids_format{};
        std::variant<hrz::blobs::BlobHandle, uint32_t> batch_ids;

        hrz::BlobArray<uint64_t> feature_ids;
    };

    static std::unique_ptr<PointCloud> create(
        hrz::Render* render,
        const hrz::monitoring::ResourceOwner& owner,
        const Geometry& geometry,
        const UniformData& uniform_data);

    PointCloud() = default;
    HRZ_DELETE_COPY_MOVE(PointCloud);
    virtual ~PointCloud() = default;

    virtual void update_selection(const hrz::flat_hash_set<uint64_t>& selected_features) = 0;

    virtual void update_feature_colors(
        gsl::span<const lm::ubvec4> colors,
        bool has_transparent_color) = 0;

    virtual void update_uniform_data(const UniformData& uniform_data) = 0;

    virtual void draw(hrz::Render* render, hrz::SceneViewBitset scene_views) = 0;

    virtual void destroy(std::vector<my::ResourceHandle>& to_destroy) = 0;
};

} // namespace hrz
