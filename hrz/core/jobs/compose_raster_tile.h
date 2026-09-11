// SPDX-FileCopyrightText: Copyright 2025 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/common/blob_image.h"
#include "hrz/common/tile_coords.h"
#include "hrz/protocol/raster/blending.pb.h"
#include "hrz/protocol/raster/sampling.pb.h"

namespace hrz_jobs
{

struct RasterTileCompositionParams
{
    struct ReprojectionMesh
    {
        std::vector<float> grid;
        lm::uvec2 quad_count;
        lm::bbox2 uv_clip;
        hrz::TileCoords tile_coords;
        hrz::TileCoords tile_image_coords;
    };

    struct Blit
    {
        hrz::TileCoords input_coords;
        lm::bbox2 uv_clip;
    };

    struct ImageWithCanvas
    {
        std::variant<ReprojectionMesh, Blit> canvas;
        hrz::BlobImage image;
        hrz_proto::RasterNodata nodata;
        hrz_proto::RasterSampling sampling;
        hrz_proto::RasterBlending blending;
        lm::bbox2 dst_uv_clip;
    };

    std::vector<ImageWithCanvas> images;
    hrz::TileCoords output_coords;
    hrz_proto::ImageFormat output_format;

    bool compute_value_bounds;
    double (*pixel_to_value)(const void* pixel);
};

struct RasterTileCompositionResponse
{
    hrz::BlobImage image;
    std::optional<double> min_value;
    std::optional<double> max_value;
};

} // namespace hrz_jobs
