#include "hrz/common/crs_utils.h"

#include "hrz/fnd/string_utils.h"

#include <assert.h>

namespace
{

constexpr bool is_digit(const char c)
{
    return c >= '0' && c <= '9';
}

std::optional<hrz::crs::Srid> parse_single_comma_srid(
    std::string_view authority,
    std::string_view str)
{
    assert(str.starts_with(authority) && str[authority.size()] == ':');

    str = str.substr(authority.size() + 1);

    if (str.size() < 1) return std::nullopt;

    if (hrz::str::find(str, ':') != -1)
    {
        // Version number is present.

        while (is_digit(str[0]) || str[0] == '.')
        {
            str = str.substr(1);
        }

        if (str[0] != ':')
        {
            // Malformed version string.
            return std::nullopt;
        }

        str = str.substr(1);
    }

    for (unsigned int i = 0; i < str.size(); ++i)
    {
        const char c = str[i];
        if (c == 0) break;
        if (!is_digit(c)) return std::nullopt;
    }

    const int code = std::atoi(str.data());
    if (code < 0 || code > std::numeric_limits<uint16_t>::max())
    {
        return std::nullopt;
    }

    return {{authority, (unsigned int)code}};
}

} // namespace

namespace hrz::crs
{

std::optional<Srid> parse_srid(std::string_view str)
{
    if (str.starts_with("urn:ogc:def:crs:"))
    {
        str = str.substr(16);
    }

    if (str == std::string_view("OGC:1.3:CRS84") || str == std::string_view("OGC:2:84"))
    {
        return {{str.substr(0, 3), 84}};
    }

    if (str == std::string_view("CRS::84"))
    {
        return {{str.substr(0, 3), 84}};
    }

    if (str.starts_with("EPSG:"))
    {
        return parse_single_comma_srid("EPSG", str);
    }
    else if (str.starts_with("OSGEO:"))
    {
        return parse_single_comma_srid("OSGEO", str);
    }

    return std::nullopt;
}

} // namespace hrz::crs
