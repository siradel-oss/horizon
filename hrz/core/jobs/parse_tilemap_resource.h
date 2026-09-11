// SPDX-FileCopyrightText: Copyright 2025 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/common/blob_allocator.h"
#include "hrz/common/planet/tiled_raster_geometry.h"

namespace hrz_jobs
{

struct TilemapResourceParams
{
    hrz::blobs::BlobHandle raw_xml;
};

struct TilemapResourceResponse
{
    std::vector<std::string> url_patterns;
    hrz::planet::TiledRasterGeometry geometry;
    std::string attribution_title;
    std::string attribution_logo;
};

} // namespace hrz_jobs
