// SPDX-FileCopyrightText: Copyright 2025 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/common/vector_data/attribute_types_defs.h"

#include <assert.h>

#include <string>

namespace hrz::vector_data
{

struct OwnedAttributeValueTraits
{
    using Type = std::variant<std::nullptr_t, bool, double, uint64_t, int64_t, std::string>;
    using ReadContext = void;
    using WriteContext = void;

    static RefAttributeValueTraits::Type as_ref(const Type& value, empty);

    static inline Type empty_string() { return std::string(); }

    static constexpr AttributeValueType type(const Type& value)
    {
        switch (value.index())
        {
            case hrz::index_of_variant<Type, std::nullptr_t>(): return AttributeValueType::kNull;
            case hrz::index_of_variant<Type, bool>(): return AttributeValueType::kBoolean;
            case hrz::index_of_variant<Type, double>(): return AttributeValueType::kNumber;
            case hrz::index_of_variant<Type, uint64_t>(): return AttributeValueType::kUint64;
            case hrz::index_of_variant<Type, int64_t>(): return AttributeValueType::kInt64;
            case hrz::index_of_variant<Type, std::string>(): return AttributeValueType::kString;
            default: assert(false && "Unhandled case"); return AttributeValueType::kNull;
        }
    }

    static inline Type null() { return std::nullptr_t{}; }

    static inline Type from_bool(bool value) { return value; }

    static inline Type from_number(double value) { return value; }

    // @Safety: value must not be a safe integer. Otherwise use from_number.
    static inline Type from_uint64(unsafe, uint64_t value, empty) { return value; }

    // @Safety: value must not be a safe integer or positive. Otherwise use from_number or
    // from_uint64.
    static inline Type from_int64(unsafe, int64_t value, empty) { return value; }

    static inline Type from_string(std::string_view value, empty) { return std::string(value); }

    // @Safety: type must be checked to be kBoolean
    static inline bool get_bool(unsafe, const Type& value, empty) { return std::get<bool>(value); }

    // @Safety: type must be checked to be kNumber
    static inline double get_number(unsafe, const Type& value, empty)
    {
        return std::get<double>(value);
    }

    // @Safety: type must be checked to be kUint64
    static inline uint64_t get_uint64(unsafe, const Type& value, empty)
    {
        return std::get<uint64_t>(value);
    }

    // @Safety: type must be checked to be kInt64
    static inline int64_t get_int64(unsafe, const Type& value, empty)
    {
        return std::get<int64_t>(value);
    }

    // @Safety: type must be checked to be kString
    static inline std::string_view get_string(unsafe, const Type& value, empty)
    {
        return std::get<std::string>(value);
    }
};

template<>
struct AttributeValueToTraits<typename OwnedAttributeValueTraits::Type>
{
    using Traits = OwnedAttributeValueTraits;
};

using OwnedAttributeValue = typename OwnedAttributeValueTraits::Type;

} // namespace hrz::vector_data
