// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/common/profiling.h"
#include "hrz/common/three_d_tiles.h"
#include "hrz/common/vertex_utils.h"
#include "hrz/core/jobs/jobs_tickets.h"
#include "hrz/core/jobs/three_d_tiles_jobs_params.h"
#include "hrz/core/point_cloud.h"
#include "hrz/core/three_d_tiles/feature_table_parser.h"
#include "hrz/core/three_d_tiles/tile_types.h"
#include "hrz/fnd/flat_hash_set.h"
#include "hrz/fnd/observed.h"
#include "mycelium/backend.h"

#include <cassert>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace hrz::three_d_tiles
{

bool ThreeDTile::PntsContent::decode_pnts_feature_table(
    ThreeDTile::Subtile* subtile,
    std::span<const std::byte> feature_table_json_data,
    const hrz::blobs::BlobHandle& feature_table_bin_blob,
    hrz::BlobAllocator* ba)
{
    HRZ_SCOPED_SAMPLE("decode pnts feature table");

    if (feature_table_json_data.empty())
    {
        return true;
    }

    const auto feature_table_bin_data = feature_table_bin_blob.get_data();

    auto parser_opt = hrz::three_d_tiles::FeatureTableParser::make(
        feature_table_json_data, feature_table_bin_blob);
    if (!parser_opt.has_value())
    {
        HRZ_LOG_ERROR("Could not create feature table parser");
        return false;
    }

    const auto parser = std::move(parser_opt.value());

    auto& content = std::get<ThreeDTile::PntsContent>(subtile->content);
    auto& geometry = content._geometry.emplace();

    subtile->batch_length = parser.get_global_uint32("BATCH_LENGTH").value_or(0);
    geometry.batch_count = subtile->batch_length;

    geometry.point_count = parser.get_global_uint32("POINTS_LENGTH").value_or(0);
    if (geometry.point_count == 0)
    {
        return true;
    }

    const auto rtc_center = parser.get_global_dvec3("RTC_CENTER").value_or(lm::dvec3{});
    subtile->rtc_transform = lm::translation(rtc_center);

    if (parser.has_semantic("POSITION"))
    {
        geometry.quantized_volume_offset = lm::dvec3(0.0);
        geometry.quantized_volume_scale = lm::dvec3(1.0);

        auto positions_opt = parser.get_blob_array<lm::vec3>("POSITION", geometry.point_count, ba);
        if (!positions_opt.has_value())
        {
            HRZ_LOG_ERROR("Incorrect alignment of position data");
            return false;
        }

        geometry.positions = std::move(positions_opt).value();
        geometry.positions_format = my::VertexFormat::Float32_3;
    }
    else if (parser.has_semantic("POSITION_QUANTIZED"))
    {
        auto quantized_volume_offset = parser.get_global_dvec3("QUANTIZED_VOLUME_OFFSET");
        auto quantized_volume_scale = parser.get_global_dvec3("QUANTIZED_VOLUME_SCALE");

        if (!quantized_volume_offset.has_value() || !quantized_volume_scale.has_value())
        {
            HRZ_LOG_ERROR(
                "Missing either QUANTIZED_VOLUME_OFFSET or QUANTIZED_VOLUME_SCALE "
                "in pnts tile");
            return false;
        }

        auto positions_opt =
            parser.get_blob_array<lm::usvec3>("POSITION_QUANTIZED", geometry.point_count, ba);
        if (!positions_opt.has_value())
        {
            HRZ_LOG_ERROR("Incorrect alignment of position data");
            return false;
        }

        geometry.quantized_volume_offset = quantized_volume_offset.value();
        geometry.quantized_volume_scale = quantized_volume_scale.value();
        geometry.positions = std::move(positions_opt).value();
        geometry.positions_format = my::VertexFormat::UInt16Norm_3;
    }
    else
    {
        HRZ_LOG_ERROR("Pnts feature table is missing points positions");
        return false;
    }

    if (parser.has_semantic("RGBA"))
    {
        auto colors_opt = parser.get_blob_array<lm::ubvec4>("RGBA", geometry.point_count, ba);
        if (!colors_opt.has_value())
        {
            HRZ_LOG_ERROR("Incorrect alignment of color data");
            return false;
        }

        {
            auto colors_data = colors_opt->get_data();
            for (const lm::ubvec4 c : colors_data)
            {
                if (c.a > 0 && c.a < 255)
                {
                    geometry.has_transparent_color = true;
                    break;
                }
            }
        }

        geometry.colors_rate = my::VertexRate::PerVertex;
        geometry.colors = std::move(colors_opt).value();
        geometry.colors_format = my::VertexFormat::UInt8Norm_4;
    }
    else if (parser.has_semantic("RGB"))
    {
        auto colors_opt = parser.get_blob_array<lm::ubvec3>("RGB", geometry.point_count, ba);
        if (!colors_opt.has_value())
        {
            HRZ_LOG_ERROR("Incorrect alignment of color data");
            return false;
        }

        geometry.has_transparent_color = false;
        geometry.colors_rate = my::VertexRate::PerVertex;
        geometry.colors = std::move(colors_opt).value();
        geometry.colors_format = my::VertexFormat::UInt8Norm_3;
    }
    else if (parser.has_semantic("RGB565"))
    {
        auto compressed_colors_opt =
            parser.get_blob_array<uint16_t>("RGB565", geometry.point_count, ba);
        if (!compressed_colors_opt.has_value())
        {
            HRZ_LOG_ERROR("Incorrect alignment of color data");
            return false;
        }

        geometry.compressed_colors = std::move(compressed_colors_opt);
        geometry.decompressed_colors_allocation =
            hrz::BlobArrayAllocation<lm::ubvec3>::allocate(ba, geometry.point_count);

        // `geometry.colors` will be filled when the blob array allocation has finished.
    }
    else
    {
        const lm::vec4 default_color =
            lm::vec4(parser.get_global_dvec4("CONSTANT_RGBA").value_or(lm::dvec4{1.0}));

        geometry.has_transparent_color = default_color.a < 1.0F;
        geometry.colors_rate = my::VertexRate::Constant;
        geometry.colors = hrz::convert_rgba_color_to_bytes(default_color);
        geometry.colors_format = my::VertexFormat::UInt8Norm_4;
    }

    if (parser.has_semantic("NORMAL"))
    {
        auto normals_opt = parser.get_blob_array<lm::vec3>("NORMAL", geometry.point_count, ba);
        if (!normals_opt.has_value())
        {
            HRZ_LOG_ERROR("Incorrect alignment of normal data");
            return false;
        }

        geometry.normals = std::move(normals_opt).value();
        geometry.compressed_normals_allocation =
            hrz::BlobArrayAllocation<uint16_t>::allocate(ba, geometry.point_count);

        // `geometry.compressed_normals` will be filled when the blob array allocation has
        // finished.
    }
    else if (parser.has_semantic("NORMAL_OCT16P"))
    {
        auto compressed_normals_opt =
            parser.get_blob_array<uint16_t>("NORMAL_OCT16P", geometry.point_count, ba);
        if (!compressed_normals_opt.has_value())
        {
            HRZ_LOG_ERROR("Incorrect alignment of normal data");
            return false;
        }

        geometry.compressed_normals_rate = my::VertexRate::PerVertex;
        geometry.compressed_normals = std::move(compressed_normals_opt).value();
    }
    else
    {
        geometry.compressed_normals_rate = my::VertexRate::Constant;
        geometry.compressed_normals =
            (uint16_t)hrz::octahedral_compress_normal<8>(lm::vec3{0, 0, 1});
    }

    if (subtile->batch_length > 0 && parser.has_semantic("BATCH_ID"))
    {
        auto component_type = parser.get_semantic_component_type(
            "BATCH_ID", hrz::three_d_tiles::AttributeComponentType::UNSIGNED_SHORT);
        switch (component_type)
        {
            case hrz::three_d_tiles::AttributeComponentType::UNSIGNED_BYTE:
            {
                auto batch_ids_opt =
                    parser.get_blob_array<uint8_t>("BATCH_ID", geometry.point_count, ba);
                if (!batch_ids_opt.has_value())
                {
                    HRZ_LOG_ERROR("Incorrect alignment of batch IDs");
                    return false;
                }

                geometry.batch_ids = std::move(batch_ids_opt).value();
                geometry.batch_ids_format = my::VertexFormat::UInt8;
                geometry.batch_ids_rate = my::VertexRate::PerVertex;
                break;
            }
            case hrz::three_d_tiles::AttributeComponentType::UNSIGNED_SHORT:
            {
                auto batch_ids_opt =
                    parser.get_blob_array<uint16_t>("BATCH_ID", geometry.point_count, ba);
                if (!batch_ids_opt.has_value())
                {
                    HRZ_LOG_ERROR("Incorrect alignment of batch IDs");
                    return false;
                }

                geometry.batch_ids = std::move(batch_ids_opt).value();
                geometry.batch_ids_format = my::VertexFormat::UInt16;
                geometry.batch_ids_rate = my::VertexRate::PerVertex;
                break;
            }
            case hrz::three_d_tiles::AttributeComponentType::UNSIGNED_INT:
            {
                auto batch_ids_opt =
                    parser.get_blob_array<uint32_t>("BATCH_ID", geometry.point_count, ba);
                if (!batch_ids_opt.has_value())
                {
                    HRZ_LOG_ERROR("Incorrect alignment of batch IDs");
                    return false;
                }

                geometry.batch_ids = std::move(batch_ids_opt).value();
                geometry.batch_ids_format = my::VertexFormat::UInt32;
                geometry.batch_ids_rate = my::VertexRate::PerVertex;
                break;
            }
            default:
                HRZ_LOG_ERROR(
                    "Unexpected component type '{}' for 'BATCH_ID' semantic in pnts tile",
                    (int)component_type);
                return false;
        }
    }
    else
    {
        geometry.batch_ids = 0U;
        geometry.batch_ids_format = my::VertexFormat::UInt32;
        geometry.batch_ids_rate = my::VertexRate::Constant;
    }

    return true;
}

std::optional<ThreeDTile::Subtile> ThreeDTile::PntsContent::load_content(
    const ContentWorkContext& ctx,
    const TilesetConfig* tileset_config,
    const hrz::blobs::BlobHandle& tile_data_blob,
    const ThreeDTile::SubtileIdentification& ident)
{
    const auto tile_data = tile_data_blob.get_data();
    const size_t tile_data_size = tile_data.size();

    const auto magic = tile_data.subspan(0, 4);

    if (std::strncmp((const char*)magic.data(), "pnts", 4) != 0) return std::nullopt;

    const auto header_size = 28;
    if (tile_data_size < header_size)
    {
        HRZ_LOG_ERROR("Not enough data received for pnts tile: {}", tile_data_size);
        return std::nullopt;
    }

    const auto header_data = tile_data.subspan(0, 28);

    auto format_version = hrz::read_le_unsafe<uint32_t>(header_data, 4);
    if (format_version != 1)
    {
        HRZ_LOG_ERROR("Unsupported pnts version: {}", format_version);
        return std::nullopt;
    }

    const auto feature_table_json_size = hrz::read_le_unsafe<uint32_t>(header_data, 12);
    const auto feature_table_bin_size = hrz::read_le_unsafe<uint32_t>(header_data, 16);
    const auto batch_table_json_size = hrz::read_le_unsafe<uint32_t>(header_data, 20);
    const auto batch_table_bin_size = hrz::read_le_unsafe<uint32_t>(header_data, 24);

    if (header_size + feature_table_json_size + feature_table_bin_size > tile_data_size)
    {
        HRZ_LOG_ERROR("Invalid pnts header");
        return std::nullopt;
    }

    const hrz::monitoring::ResourceOwner owner{
        hrz::monitoring::systems::ThreeDTilesLayers, tileset_config->global_layer_id
    };

    ThreeDTile::Subtile subtile;
    auto& content = subtile.content.emplace<ThreeDTile::PntsContent>();

    const auto feature_table_json_data = tile_data.subspan(header_size, feature_table_json_size);
    const auto feature_table_bin_blob = tile_data_blob.make_sub_blob(
        hrz::unsafe("File size has been checked above"), header_size + feature_table_json_size,
        feature_table_bin_size);
    if (!decode_pnts_feature_table(
            &subtile, feature_table_json_data, feature_table_bin_blob, ctx.ba))
    {
        return std::nullopt;
    }

    {
        hrz_jobs::EncodedBatchTable params;
        params.batch_length = subtile.batch_length;
        params.json_data = hrz::blobs::make_sub_blob(
            hrz::unsafe("File size has been checked above"), ctx.ba, tile_data_blob,
            header_size + feature_table_json_size + feature_table_bin_size, batch_table_json_size);
        params.bin_data = hrz::blobs::make_sub_blob(
            hrz::unsafe("File size has been checked above"), ctx.ba, tile_data_blob,
            header_size + feature_table_json_size + feature_table_bin_size + batch_table_json_size,
            batch_table_bin_size);
        params.attributes = tileset_config->attributes;
        subtile.decode_batch_table_ticket =
            hrz_jobs::add_job_decode_three_d_tiles_batch_table(ctx.js, std::move(params), owner);
    }

    content.update_appearance(tileset_config->inherited_draw_prps);

    content._uniform_data.mutate(
        [&ident](hrz::PointCloud::UniformData& data)
        {
            data.object_reference = ident.object_reference.to_uvec2();
            data.feature_reference = ident.feature_reference.to_uvec3();
            data.object_id_offset = ident.batch_id_offset;
        });

    return std::move(subtile);
}

ThreeDTile::ContentLoadStatusUpdate ThreeDTile::PntsContent::work_loading(
    const ContentWorkContext& ctx,
    const ThreeDTile& /* tile */,
    const ThreeDTile::Subtile& subtile,
    const TilesetConfig* tileset_config)
{
    if (auto& allocation = _geometry->decompressed_colors_allocation; allocation.has_value())
    {
        assert(_geometry->compressed_colors.has_value());

        switch (allocation->get_state(ctx.ba))
        {
            case hrz::BlobArrayAllocationState::Allocated:
            {
                auto colors_array = allocation->to_array(ctx.ba);
                colors_array.register_blob_owner(
                    ctx.ba,
                    {hrz::monitoring::systems::ThreeDTilesLayers, tileset_config->global_layer_id});
                colors_array.register_blob_metadata(ctx.ba, "contents"_ss, "point cloud colors"_ss);

                {
                    auto colors_data = colors_array.get_mutable_data();
                    const auto compressed_colors_data = _geometry->compressed_colors->get_data();

                    for (size_t i = 0; i < _geometry->point_count; ++i)
                    {
                        const auto color = compressed_colors_data[i];
                        colors_data[i] = lm::ubvec3(
                            (color & 0xf800) >> 8, (color & 0x07e0) >> 3, (color & 0x001f) << 3);
                    }
                }

                _geometry->compressed_colors = std::nullopt;
                _geometry->decompressed_colors_allocation = std::nullopt;
                _geometry->colors = std::move(colors_array);
                break;
            }
            case hrz::BlobArrayAllocationState::Error:
            {
                HRZ_LOG_ERROR("Could not allocate color array for point cloud tile");
                return ContentLoadStatusUpdate::ERROR;
            }
            default: break;
        }
    }

    if (auto& allocation = _geometry->compressed_normals_allocation; allocation.has_value())
    {
        assert(_geometry->normals.has_value());

        switch (allocation->get_state(ctx.ba))
        {
            case hrz::BlobArrayAllocationState::Allocated:
            {
                auto compressed_normals_array = allocation->to_array(ctx.ba);
                compressed_normals_array.register_blob_owner(
                    ctx.ba,
                    {hrz::monitoring::systems::ThreeDTilesLayers, tileset_config->global_layer_id});
                compressed_normals_array.register_blob_metadata(
                    ctx.ba, "contents"_ss, "point cloud normals"_ss);

                {
                    auto compressed_normals_data = compressed_normals_array.get_mutable_data();
                    const auto normals_data = _geometry->normals->get_data();

                    for (size_t i = 0; i < _geometry->point_count; ++i)
                    {
                        compressed_normals_data[i] =
                            (uint16_t)hrz::octahedral_compress_normal<8>(normals_data[i]);
                    }
                }

                _geometry->normals = std::nullopt;
                _geometry->compressed_normals_allocation = std::nullopt;

                _geometry->compressed_normals = std::move(compressed_normals_array);
                break;
            }
            case hrz::BlobArrayAllocationState::Error:
            {
                HRZ_LOG_ERROR("Could not allocate normal array for point cloud tile");
                return ContentLoadStatusUpdate::ERROR;
            }
            default: break;
        }
    }

    if (!_geometry->decompressed_colors_allocation.has_value()
        && !_geometry->compressed_normals_allocation.has_value() && subtile.batch_table_decoded)
    {
        return ContentLoadStatusUpdate::LOADED;
    }
    else
    {
        return ContentLoadStatusUpdate::STILL_LOADING;
    }
}

bool ThreeDTile::PntsContent::geometry_has_positions() const
{
    if (!_geometry.has_value())
    {
        return false;
    }
    else if (_geometry->positions_format == my::VertexFormat::Float32_3)
    {
        return std::get<hrz::BlobArray<lm::vec3>>(_geometry->positions).blob().is_valid();
    }
    else if (_geometry->positions_format == my::VertexFormat::UInt16Norm_3)
    {
        return std::get<hrz::BlobArray<lm::usvec3>>(_geometry->positions).blob().is_valid();
    }
    else
    {
        return false;
    }
}

hrz::RenderRequest ThreeDTile::PntsContent::work_gpu(
    const ContentWorkContext& /* ctx */,
    const ContentRenderContext& render_ctx,
    const ThreeDTile& tile,
    const ThreeDTile::Subtile& subtile,
    const TilesetConfig* tileset_config)
{
    // Already uploaded or empty
    if (_point_cloud || (_geometry.has_value() && _geometry->point_count == 0)) return {};

    // Data not available yet
    if (!geometry_has_positions() || !subtile.batch_table_decoded) return {};

    _uniform_data.mutate(
        [&tile, &subtile, this](hrz::PointCloud::UniformData& uniform)
        {
            const lm::dmat4 transform = tile.base_transform * subtile.rtc_transform;
            uniform.linear_transform = lm::mat3(transform);
            uniform.normal_matrix = lm::mat3(hrz::compute_normal_transform_matrix(transform));

            const lm::dvec3 offset = transform.w.xyz;
            hrz::split_double(offset.x, uniform.rtc_low.x, uniform.rtc_high.x);
            hrz::split_double(offset.y, uniform.rtc_low.y, uniform.rtc_high.y);
            hrz::split_double(offset.z, uniform.rtc_low.z, uniform.rtc_high.z);

            uniform.quantized_position_scale = lm::vec3(_geometry->quantized_volume_scale);
            uniform.quantized_position_offset = lm::vec3(_geometry->quantized_volume_offset);
        });

    _geometry->feature_ids = subtile.batches_to_feature_ids.hashes();

    _point_cloud = hrz::PointCloud::create(
        render_ctx.render,
        {hrz::monitoring::systems::ThreeDTilesLayers, tileset_config->global_layer_id},
        _geometry.value(), _uniform_data.get());

    update_feature_colors(subtile.per_batch_color, subtile.has_transparent_batch);
    update_selection(tileset_config->selected_feature_ids);

    _geometry = std::nullopt;

    return {};
}

void ThreeDTile::PntsContent::draw(
    const ContentWorkContext& /* ctx */,
    const ContentRenderContext& render_ctx,
    hrz::SceneViewBitset draw_in)
{
    if (_point_cloud)
    {
        if (_uniform_data.is_dirty())
        {
            _point_cloud->update_uniform_data(_uniform_data.get());
        }

        _point_cloud->draw(render_ctx.render, draw_in.bits());
    }
}

void ThreeDTile::PntsContent::update_selection(
    const hrz::flat_hash_set<hrz::vector_data::FeatureIdHash>& selected_feature_ids)
{
    if (_point_cloud)
    {
        _point_cloud->update_selection(selected_feature_ids);
    }
}

void ThreeDTile::PntsContent::update_feature_colors(
    std::span<const lm::ubvec4> per_batch_colors,
    bool has_transparent_color)
{
    if (_point_cloud)
    {
        _point_cloud->update_feature_colors(per_batch_colors, has_transparent_color);
    }
}

void ThreeDTile::PntsContent::unload(
    const ContentWorkContext& /* ctx */,
    std::vector<my::ResourceHandle>& to_destroy)
{
    _geometry = std::nullopt;

    if (_point_cloud)
    {
        _point_cloud->destroy(to_destroy);
        _point_cloud.reset();
    }
}

} // namespace hrz::three_d_tiles
