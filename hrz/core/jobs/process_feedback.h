#pragma once

#include "hrz/common/blob_image.h"
#include "hrz/common/planet/tile_request.h"

namespace hrz_jobs
{

struct FeedbackData
{
    hrz::BlobImage image;
    std::vector<lm::uvec2> clipmap_offsets;
};

struct TileList
{
    std::vector<hrz::planet::RequestedTileCoords> tile_usage;
};

} // namespace hrz_jobs
