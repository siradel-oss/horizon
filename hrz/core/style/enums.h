#pragma once

#include <cstdint>
#include <string_view>

namespace hrz::style
{
struct EnumFindResult
{
    enum class Type
    {
        Ok,
        UnknownEnum,
        UnknownValue,
    };

    Type type;
    uint64_t value;
};

EnumFindResult find_enum_value_by_name(std::string_view enum_name, std::string_view value_name);
} // namespace hrz::style
