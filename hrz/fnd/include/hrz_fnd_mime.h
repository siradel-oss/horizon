#pragma once

#include "hrz_fnd_inlined_vector.h"

#include <optional>
#include <string_view>

namespace hrz
{

struct ParsedMime
{
    using Parameter = std::pair<std::string_view, std::string_view>;

    std::string_view type;
    std::string_view subtype;
    hrz::InlinedVector<std::string_view, 2> suffixes;
    hrz::InlinedVector<Parameter, 4> parameters;

    std::optional<std::string_view> get_parameter(std::string_view key) const;

    bool has_parameter(std::string_view key) const { return get_parameter(key).has_value(); }

    std::string to_string() const;
};

ParsedMime parse_mime(std::string_view);

} // namespace hrz
