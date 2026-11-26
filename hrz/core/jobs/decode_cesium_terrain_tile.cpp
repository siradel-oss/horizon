// SPDX-FileCopyrightText: Copyright 2022 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/core/jobs/decode_cesium_terrain_tile.h"

#include "hrz/common/blob_allocator.h"
#include "hrz/common/blob_image.h"
#include "hrz/common/image_view.h"
#include "hrz/common/profiling.h"
#include "hrz/core/jobs/context.h"
#include "hrz/core/jobs/job_result.h"
#include "hrz/core/jobs/rasterizer.h"
#include "hrz/fnd/log.h"
#include "hrz/fnd/maths.h"
#include "hrz/fnd/mem.h"

#include <lin_maths.h>

#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace hrz_jobs::rasterize_cesium_terrain_tile
{
namespace
{

static_assert(sizeof(double) == 8, "sizeof(double) != 8");
static_assert(sizeof(float) == 4, "sizeof(float) != 4");

// https://github.com/CesiumGS/cesium/wiki/heightmap-1.0
hrz_jobs::JobResult decode_heightmap_tile(
    std::span<const std::byte> raw_data,
    hrz::BlobImage& output,
    const JobContext& context)
{
    HRZ_SCOPED_SAMPLE("decode cesium terrain heightmap tile");

    auto blob_allocator = context.get_blob_allocator();

    if (raw_data.size() < 8450)
    {
        HRZ_LOG_ERROR("Incorrect tile data size: {}", raw_data.size());
        return hrz_jobs::JobResult::FAILURE;
    }

    float elevation_data[65 * 65];

    for (size_t i = 0; i < 65 * 65; ++i)
    {
        const auto encoded_elevation =
            hrz::read_le_unsafe<uint16_t>(raw_data, sizeof(uint16_t) * i);
        elevation_data[i] = (float)encoded_elevation / 5.0F - 1000.0F;
    }

    const auto elevation_at = [&](size_t x, size_t y) { return elevation_data[x + y * 65]; };

    const size_t output_data_size = 64 * 64 * sizeof(float);
    auto output_blob_opt = hrz::blobs::allocate_blob_sync(blob_allocator, output_data_size);
    if (!output_blob_opt.has_value())
    {
        HRZ_LOG_ERROR("Could not allocate output image");
        return hrz_jobs::JobResult::FAILURE;
    }

    hrz::blobs::register_owner(
        context.get_blob_allocator(), output_blob_opt.value(), context.get_resource_owner());
    auto output_blob_data = output_blob_opt->get_mutable_data();
    const auto output_blob_data_span = output_blob_data.as_span();

    // Values in the source array represent the elevations at the edges of the pixels.
    // Here the values are interpolated to make a bitmap where values correspond to the
    // pixel centres.
    for (size_t i = 0; i < 64 * 64; ++i)
    {
        const uint32_t x = i % 64;
        const uint32_t y = i / 64;

        const float elevation = (elevation_at(x, y) + elevation_at(x + 1, y)
                                 + elevation_at(x, y + 1) + elevation_at(x + 1, y + 1))
            / 4;

        hrz::write_le_unsafe(
            output_blob_data_span, i * sizeof(float), std::bit_cast<uint32_t>(elevation));
    }

    output_blob_data.release();

    output = hrz::BlobImage::make(
        hrz_proto::ImageFormat::R_F32, 64, 64, std::move(output_blob_opt.value()),
        context.get_blob_allocator());

    return hrz_jobs::JobResult::SUCCESS;
}

static constexpr size_t QuantizedMeshHeaderSize = sizeof(double) * 10 + sizeof(float) * 2;

struct Vertex
{
    lm::vec2 position;
    float elevation{};
};

uint16_t zigzag_decode(uint16_t value)
{
    return (value >> 1) ^ (-(value & 1));
}

struct SampleAndComposeFunction : public rasterizer::SampleAndComposeFunction<float>
{
    SampleAndComposeFunction() = default;
    ~SampleAndComposeFunction() override = default;

    bool sample_and_compose(int pixel_x, int pixel_y, float elevation, hrz::MutImageView& output)
        override
    {
        *output.pixel_data<float, 1>(pixel_x, pixel_y) = elevation;
        return true;
    }
};

// https://github.com/CesiumGS/quantized-mesh
hrz_jobs::JobResult decode_quantized_mesh_tile(
    std::span<const std::byte> raw_data,
    hrz::BlobImage& output,
    const JobContext& context)
{
    HRZ_SCOPED_SAMPLE("decode cesium terrain quantized mesh tile");

    auto blob_allocator = context.get_blob_allocator();

    if (raw_data.size() < QuantizedMeshHeaderSize + sizeof(uint32_t))
    {
        HRZ_LOG_ERROR("Tile data too small");
        return hrz_jobs::JobResult::FAILURE;
    }

    const auto minimum_height =
        std::bit_cast<float>(hrz::read_le_unsafe<uint32_t>(raw_data, sizeof(double) * 3));
    const auto maximum_height = std::bit_cast<float>(
        hrz::read_le_unsafe<uint32_t>(raw_data, sizeof(double) * 3 + sizeof(float)));

    const auto* vertex_data = raw_data.data() + QuantizedMeshHeaderSize;

    const auto vertex_count = hrz::read_le_unsafe<uint32_t>(vertex_data, 0);

    {
        const size_t vertex_data_size = sizeof(uint32_t) + sizeof(uint16_t) * 3 * vertex_count;
        if (raw_data.data() + raw_data.size() < vertex_data + vertex_data_size + sizeof(uint32_t))
        {
            HRZ_LOG_ERROR("Tile data too small");
            return hrz_jobs::JobResult::FAILURE;
        }
    }

    const auto* u_array = vertex_data + sizeof(uint32_t);
    const auto* v_array = u_array + sizeof(uint16_t) * vertex_count;
    const auto* height_array = v_array + sizeof(uint16_t) * vertex_count;

    std::vector<Vertex> vertices(vertex_count);

    uint16_t u = 0;
    uint16_t v = 0;
    uint16_t height = 0;
    for (size_t i = 0; i < vertex_count; ++i)
    {
        const uint16_t zigzag_u = hrz::read_le_unsafe<uint16_t>(u_array, sizeof(uint16_t) * i);
        const uint16_t zigzag_v = hrz::read_le_unsafe<uint16_t>(v_array, sizeof(uint16_t) * i);
        const uint16_t zigzag_height =
            hrz::read_le_unsafe<uint16_t>(height_array, sizeof(uint16_t) * i);

        u += zigzag_decode(zigzag_u);
        v += zigzag_decode(zigzag_v);
        height += zigzag_decode(zigzag_height);

        const float x = (float)u / 32767.0F;
        const float y = 1.0F - (float)v / 32767.0F;
        const float elevation = hrz::lerp(minimum_height, maximum_height, (float)height / 32767.0F);

        vertices[i] = {{x, y}, elevation};
    }

    const auto* index_data = vertex_data + sizeof(uint32_t) + sizeof(uint16_t) * 3 * vertex_count;

    const uint32_t triangle_count = hrz::read_le_unsafe<uint32_t>(index_data, 0);

    {
        const size_t index_data_size = sizeof(uint32_t)
            + (vertex_count <= 65536 ? sizeof(uint16_t) : sizeof(uint32_t)) * 3 * triangle_count;
        if (raw_data.data() + raw_data.size() < index_data + index_data_size)
        {
            HRZ_LOG_ERROR("Tile data too small");
            return hrz_jobs::JobResult::FAILURE;
        }
    }

    const auto* index_array = index_data + sizeof(uint32_t);

    std::vector<uint32_t> indices(triangle_count * 3);

    uint32_t highest_index_code = 0;
    for (size_t i = 0; i < triangle_count * 3; ++i)
    {
        if (vertex_count <= 65536)
        {
            indices[i] = hrz::read_le_unsafe<uint16_t>(index_array, sizeof(uint16_t) * i);
        }
        else
        {
            indices[i] = hrz::read_le_unsafe<uint32_t>(index_array, sizeof(uint32_t) * i);
        }

        const uint32_t code = indices[i];
        indices[i] = highest_index_code - code;
        if (code == 0)
        {
            highest_index_code += 1;
        }
    }

    const size_t output_data_size = 256 * 256 * sizeof(float);
    auto output_blob_opt = hrz::blobs::allocate_blob_sync(blob_allocator, output_data_size);
    if (!output_blob_opt.has_value())
    {
        HRZ_LOG_ERROR("Could not allocate output image");
        return hrz_jobs::JobResult::FAILURE;
    }

    hrz::blobs::register_owner(
        context.get_blob_allocator(), output_blob_opt.value(), context.get_resource_owner());
    auto output_blob_data = output_blob_opt->get_mutable_data();

    hrz::MutImageView output_image(output_blob_data, hrz_proto::ImageFormat::R_F32, 256, 256);

    SampleAndComposeFunction sample_and_compose_function = {};

    for (size_t i = 0; i < triangle_count; ++i)
    {
        const auto v0 = vertices[indices[i * 3 + 0]];
        const auto v1 = vertices[indices[i * 3 + 1]];
        const auto v2 = vertices[indices[i * 3 + 2]];

        rasterizer::rasterize_triangle<float>(
            output_image, v0.position, v1.position, v2.position, v0.elevation, v1.elevation,
            v2.elevation, &sample_and_compose_function);
    }

    output_blob_data.release();

    output = hrz::BlobImage::make(
        hrz_proto::ImageFormat::R_F32, 256, 256, std::move(output_blob_opt.value()),
        context.get_blob_allocator());

    return hrz_jobs::JobResult::SUCCESS;
}

} // namespace

hrz_jobs::JobResult run(
    const hrz_jobs::CesiumTerrainTileData& params,
    hrz::BlobImage& response,
    const JobContext& context)
{
    const auto input_data = params.blob.get_data();

    auto result = hrz_jobs::JobResult::FAILURE;
    if (params.format == "heightmap-1.0")
    {
        result = decode_heightmap_tile(input_data, response, context);
    }
    else if (params.format == "quantized-mesh-1.0")
    {
        result = decode_quantized_mesh_tile(input_data, response, context);
    }
    else
    {
        HRZ_LOG_ERROR("Unsupported tile format: \"{}\"", params.format);
    }

    return result;
}

} // namespace hrz_jobs::rasterize_cesium_terrain_tile
