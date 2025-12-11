#pragma once

#include "hrz/common/tile_coords.h"

namespace hrz::planet
{

enum TileRequestOrigin : uint8_t
{
    FeedbackOrigin = 1 << 0,
    CameraVerticalProjectionOrigin = 1 << 1,
};

struct RequestedTileCoords
{
    hrz::TileCoords coords;
    uint32_t uses;
    TileRequestOrigin origin;
};

} // namespace hrz::planet
