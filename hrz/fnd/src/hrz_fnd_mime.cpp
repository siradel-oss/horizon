#include "hrz_fnd_mime.h"

#include "hrz_fnd_string_utils.h"

#include <fmt/format.h>

hrz::ParsedMime hrz::parse_mime(std::string_view all)
{
    auto [type, subtype_suffixes_params] = str::split(all, '/');

    if (subtype_suffixes_params.empty())
    {
        subtype_suffixes_params = type;
        type = "";
    }

    auto semicolon_pos = str::find(subtype_suffixes_params, ';');
    auto plus_pos = str::find(subtype_suffixes_params, '+');

    auto subtype_end_pos = std::min(
        semicolon_pos == -1 ? subtype_suffixes_params.size() : static_cast<size_t>(semicolon_pos),
        plus_pos == -1 ? subtype_suffixes_params.size() : static_cast<size_t>(plus_pos));

    hrz::ParsedMime result{type, str::trim_s(subtype_suffixes_params.substr(0, subtype_end_pos))};

    auto [suffixes, params] =
        str::split(str::ltrim_s(subtype_suffixes_params.substr(subtype_end_pos), '+'), ';');

    while (!suffixes.empty())
    {
        auto [suffix, rest] = str::split(suffixes, '+');
        result.suffixes.push_back(str::trim_s(suffix));
        suffixes = rest;
    }

    while (!params.empty())
    {
        auto [param, rest] = str::split(params, ';');
        auto [key, value] = str::split(param, '=');
        result.parameters.push_back({str::trim_s(key), value});
        params = rest;
    }

    return result;
}

std::optional<std::string_view> hrz::ParsedMime::get_parameter(std::string_view key) const
{
    for (const auto& [k, v] : parameters)
    {
        if (str::iequals(k, key))
        {
            return v;
        }
    }
    return std::nullopt;
}

std::string hrz::ParsedMime::to_string() const
{
    std::string buffer;

    fmt::format_to(std::back_inserter(buffer), "{}/{}", type, subtype);
    for (const auto& suffix : suffixes)
    {
        fmt::format_to(std::back_inserter(buffer), "+{}", suffix);
    }

    for (const auto& [key, value] : parameters)
    {
        if (value.empty())
        {
            fmt::format_to(std::back_inserter(buffer), ";{}", key);
        }
        else
        {
            fmt::format_to(std::back_inserter(buffer), ";{}={}", key, value);
        }
    }

    return buffer;
}
