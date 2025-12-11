#pragma once

#include <pugixml/pugixml.hpp>

#include <optional>
#include <string_view>

namespace hrz::ogc
{
constexpr double PixelSize = 0.00028; // m/pixel

std::optional<std::string_view> find_image_format(
    const pugi::xml_node& get_map_node,
    std::string_view desired_format,
    bool prioritize_transparent_images);

bool crs_has_flipped_axes(std::string_view authority, unsigned int srid);
} // namespace hrz::ogc
