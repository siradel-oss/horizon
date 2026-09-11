// SPDX-FileCopyrightText: Copyright 2025 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/common/blob_allocator.h"
#include "hrz/common/planet/tiled_raster_geometry.h"

namespace hrz_jobs
{

struct WmtsResourceParams
{
    std::string wmts_url;
    hrz::blobs::BlobHandle raw_xml;
    std::string layer_identifier;
    std::string style_identifier;
    std::string image_format;
};

enum class WmtsGetTileMethod
{
    GET_RESTFUL,
    GET_KVP,
};

struct WmtsResourceResponse
{
    WmtsGetTileMethod get_tile_method;
    std::vector<std::string> url_patterns;
    std::vector<std::string> matrix_identifiers;
    hrz::planet::TiledRasterGeometry geometry;
};

} // namespace hrz_jobs
