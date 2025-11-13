#pragma once

#include "hrz/fnd/static_string.h"
#include "hrz/fnd/variant.h"

#include <string>
#include <string_view>

namespace hrz
{
// This structure is used to avoid copying metadata strings
// when not necessary.
// `std::string`, `std::string_view`, and `const char*` are
// copied.
// `StaticString`s are used as-is, that is, they refer to
// static char strings.
struct MetadataString
{
public:
    MetadataString() = default;

    MetadataString(std::string str) : str(std::move(str)) {}

    MetadataString(std::string_view view) : str(std::string(view)) {}

    MetadataString(const char* str) : str(std::string(str)) {}

    MetadataString(StaticString str) : str(str) {}

    operator std::string_view() const { return to_string(); }

    std::string_view to_string() const
    {
        if (std::holds_alternative<StaticString>(str))
        {
            return std::get<StaticString>(str);
        }
        else
        {
            return std::string_view(std::get<std::string>(str));
        }
    }

    const char* data() const { return to_string().data(); }

    size_t size() const { return to_string().size(); }

    bool empty() const { return to_string().empty(); }

    bool operator==(const MetadataString& other) const { return other.to_string() == to_string(); }

private:
    std::variant<StaticString, std::string> str;
};
} // namespace hrz
