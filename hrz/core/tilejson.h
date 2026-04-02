#pragma once

#include "hrz/common/geo.h"
#include "hrz/core/base_url.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace hrz::tilejson
{

struct TileJsonInfo
{
    GeoBounds bounds;
    uint32_t min_level;
    uint32_t max_level;
    std::vector<std::string> url_patterns;
    std::string attribution;
};

std::optional<TileJsonInfo> parse_tilejson(
    std::span<const std::byte> raw_data,
    const BaseUrl& base_url);

} // namespace hrz::tilejson
