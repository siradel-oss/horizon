#pragma once

#include <optional>
#include <string_view>

namespace hrz::crs
{

// Only valid as long as the source string is alive.
struct Srid
{
    std::string_view authority;
    unsigned int code;
};

// Split an SRID string into an authority-code pair.
// Supports strings such as "EPSG:4326", or longer ones like
// "urn:ogc:def:crs:EPSG:6.18.3:3857".
// "urn:ogc:def:crs:OGC:1.3:CRS84" and "urn:ogc:def:crs:OGC:2:84"
// are returned as "OCG" and 84.
// "urn:ogc:def:crs:CRS::84" is returned as "CRS" and 84.
std::optional<Srid> parse_srid(std::string_view srid);

} // namespace hrz::crs
