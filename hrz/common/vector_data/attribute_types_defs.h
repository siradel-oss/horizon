// SPDX-FileCopyrightText: Copyright 2025 Siradel
// SPDX-License-Identifier: MIT

#pragma once

#include "hrz/common/color.h"
#include "hrz/fnd/meta.h"
#include "hrz/fnd/unsafe.h"
#include "hrz/fnd/variant.h"

#include <lin_maths.h>

#include <cassert>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <variant>

namespace hrz::vector_data
{

// Attributes can have different kind of payloads:
// - Null
// - Boolean: true or false.
// - Number: double or any integer that is "safe" (i.e. fits in a double without loss of precision).
// Can also contain colors as 0xaabbggrr and booleans.
// - Int64: 64-bit signed integer.
// - Uint64: 64-bit unsigned integer.
// - String: UTF-8 string.

// There are many ways attributes can be stored depending on where they are stored or how they are
// used:
// - RefAttributeValue: A reference to an attribute value. This is used for short lifetimes, as a
// lightweight store.
// - OwnedAttributeValue: An owned attribute value. This is used for longer lifetimes.
// - PackedAttributeValue: A packed attribute value. This is used for deep storage. They are packed
// as uint64_t and use out of line data. See below for more explanations.
// - ApiAttributeValue: A protobuf message that uses oneof. Used by API methods and in the scene
// model.

// Most of this file is dedicated to converting between these types and getting values in or out of
// their storage type.

enum class AttributeValueType
{
    kNull,
    kBoolean,
    kNumber,
    kInt64,
    kUint64,
    kString,
};

static constexpr int64_t kMaxSafeInteger = 0x001f'ffff'ffff'ffff;

constexpr bool is_safe_integer(uint64_t value)
{
    return value <= kMaxSafeInteger;
}

constexpr bool is_safe_integer(int64_t value)
{
    return value >= -(int64_t)kMaxSafeInteger && value <= (int64_t)kMaxSafeInteger;
}

inline AttributeValueType store_as(uint64_t value)
{
    if (is_safe_integer(value))
    {
        return AttributeValueType::kNumber;
    }
    else
    {
        return AttributeValueType::kUint64;
    }
}

inline AttributeValueType store_as(int64_t value)
{
    if (is_safe_integer(value))
    {
        return AttributeValueType::kNumber;
    }
    else if (value >= 0)
    {
        return AttributeValueType::kUint64;
    }
    else
    {
        return AttributeValueType::kInt64;
    }
}

template<typename WriteContextT>
struct RefAttributeValueTraitsGeneric
{
    using Type = std::variant<std::nullptr_t, bool, double, uint64_t, int64_t, std::string_view>;
    using ReadContext = void;
    using WriteContext = WriteContextT;

    static constexpr Type as_ref(const Type& value, empty) { return value; }

    static constexpr Type empty_string() { return ""; }

    static constexpr AttributeValueType type(const Type& value)
    {
        switch (value.index())
        {
            case hrz::index_of_variant<Type, std::nullptr_t>(): return AttributeValueType::kNull;
            case hrz::index_of_variant<Type, bool>(): return AttributeValueType::kBoolean;
            case hrz::index_of_variant<Type, double>(): return AttributeValueType::kNumber;
            case hrz::index_of_variant<Type, uint64_t>(): return AttributeValueType::kUint64;
            case hrz::index_of_variant<Type, int64_t>(): return AttributeValueType::kInt64;
            case hrz::index_of_variant<Type, std::string_view>():
                return AttributeValueType::kString;
            default: assert(false && "Unhandled case"); return AttributeValueType::kNull;
        }
    }

    static inline Type null() { return std::nullptr_t{}; }

    static inline Type from_bool(bool value) { return value; }

    static inline Type from_number(double value) { return value; }

    // @Safety: value must not be a safe integer. Otherwise use from_number.
    static inline Type from_uint64(unsafe, uint64_t value, devoid_t<WriteContext>&)
    {
        return value;
    }

    // @Safety: value must not be a safe integer or positive. Otherwise use from_number or
    // from_uint64.
    static inline Type from_int64(unsafe, int64_t value, devoid_t<WriteContext>&) { return value; }

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
        return std::get<std::string_view>(value);
    }
};

struct RefAttributeValueTraits : public RefAttributeValueTraitsGeneric<void>
{
    static inline Type from_string(std::string_view value, empty) { return value; }
};

template<typename Traits>
concept AttributeValueTraits =
    requires {
        typename Traits::Type;
        typename Traits::ReadContext;
        typename Traits::WriteContext;
    }
    && requires(
        typename Traits::Type value,
        const typename Traits::Type& const_value,
        const devoid_t<typename Traits::ReadContext>& read_ctx,
        devoid_t<typename Traits::WriteContext>& write_ctx,
        bool bool_value,
        double number_value,
        uint64_t uint64_value,
        int64_t int64_value,
        std::string_view string_value) {
           { Traits::type(const_value) } -> std::same_as<AttributeValueType>;
           { Traits::empty_string() } -> std::same_as<typename Traits::Type>;
           { Traits::as_ref(const_value, read_ctx) } -> std::same_as<RefAttributeValueTraits::Type>;
           { Traits::null() } -> std::same_as<typename Traits::Type>;
           { Traits::from_bool(bool_value) } -> std::same_as<typename Traits::Type>;
           { Traits::from_number(number_value) } -> std::same_as<typename Traits::Type>;
           {
               Traits::from_uint64(unsafe{"caller must ensure safety"}, uint64_value, write_ctx)
           } -> std::same_as<typename Traits::Type>;
           {
               Traits::from_int64(unsafe{"caller must ensure safety"}, int64_value, write_ctx)
           } -> std::same_as<typename Traits::Type>;
           { Traits::from_string(string_value, write_ctx) } -> std::same_as<typename Traits::Type>;
           {
               Traits::get_bool(unsafe{"caller must ensure safety"}, const_value, read_ctx)
           } -> std::same_as<bool>;
           {
               Traits::get_number(unsafe{"caller must ensure safety"}, const_value, read_ctx)
           } -> std::same_as<double>;
           {
               Traits::get_uint64(unsafe{"caller must ensure safety"}, const_value, read_ctx)
           } -> std::same_as<uint64_t>;
           {
               Traits::get_int64(unsafe{"caller must ensure safety"}, const_value, read_ctx)
           } -> std::same_as<int64_t>;
           {
               Traits::get_string(unsafe{"caller must ensure safety"}, const_value, read_ctx)
           } -> std::same_as<std::string_view>;
       };

// AttributeValueToTraits is used to get the attribute storage trait from its
// values type automatically.

template<typename T>
struct AttributeValueToTraits
{
};

template<typename T>
using AttributeValueTraitsFor = typename AttributeValueToTraits<std::remove_cvref_t<T>>::Traits;

// These first few methods don't require any read context. They are used to
// retrieve metadata about attribute values.

template<typename T, AttributeValueTraits Traits = AttributeValueTraitsFor<T>>
constexpr AttributeValueType attr_type(const T& value)
{
    return Traits::type(value);
}

template<typename T, AttributeValueTraits Traits = AttributeValueTraitsFor<T>>
constexpr bool attr_is_null(const T& value)
{
    return Traits::type(value) == AttributeValueType::kNull;
}

template<typename T, AttributeValueTraits Traits = AttributeValueTraitsFor<T>>
constexpr bool attr_is_bool(const T& value)
{
    return Traits::type(value) == AttributeValueType::kBoolean;
}

template<typename T, AttributeValueTraits Traits = AttributeValueTraitsFor<T>>
constexpr bool attr_is_number(const T& value)
{
    return Traits::type(value) == AttributeValueType::kNumber;
}

template<typename T, AttributeValueTraits Traits = AttributeValueTraitsFor<T>>
constexpr bool attr_is_uint64(const T& value)
{
    return Traits::type(value) == AttributeValueType::kUint64;
}

template<typename T, AttributeValueTraits Traits = AttributeValueTraitsFor<T>>
constexpr bool attr_is_int64(const T& value)
{
    return Traits::type(value) == AttributeValueType::kInt64;
}

template<typename T, AttributeValueTraits Traits = AttributeValueTraitsFor<T>>
constexpr bool attr_is_64bit_integer(const T& value)
{
    auto type = Traits::type(value);
    return type == AttributeValueType::kUint64 || type == AttributeValueType::kInt64;
}

template<typename T, AttributeValueTraits Traits = AttributeValueTraitsFor<T>>
constexpr bool attr_is_string(const T& value)
{
    return Traits::type(value) == AttributeValueType::kString;
}

template<typename T, AttributeValueTraits Traits = AttributeValueTraitsFor<T>>
inline T attr_null()
{
    return Traits::null();
}

template<typename T, AttributeValueTraits Traits = AttributeValueTraitsFor<T>>
inline T attr_empty_string()
{
    return Traits::empty_string();
}

// Now we get into read and write methods. These methods do require context,
// hence the slight complication. This is because we want these methods to be
// usable on types that don't require context without having to provide a void
// or empty type every time. Hence why we need all the `requires` clauses.

template<typename T, AttributeValueTraits Traits = AttributeValueTraitsFor<T>>
RefAttributeValueTraits::Type attr_as_ref(
    const T& value,
    const devoid_t<typename Traits::ReadContext>& ctx)
{
    return Traits::as_ref(value, ctx);
}

template<typename T, AttributeValueTraits Traits = AttributeValueTraitsFor<T>>
inline RefAttributeValueTraits::Type attr_as_ref(const T& value)
    requires std::is_void_v<typename Traits::ReadContext>
{
    return Traits::as_ref(value, empty{});
}

#define ATTR_READ_IMPL(NAME, TYPE, ENUM_TYPE)                                               \
    template<typename T, AttributeValueTraits Traits = AttributeValueTraitsFor<T>>          \
    TYPE attr_get_##NAME(const T& value, const devoid_t<typename Traits::ReadContext>& ctx) \
    {                                                                                       \
        if (attr_type<T, Traits>(value) == AttributeValueType::ENUM_TYPE)                   \
        {                                                                                   \
            return Traits::get_##NAME(hrz::unsafe("type is checked"), value, ctx);          \
        }                                                                                   \
        else                                                                                \
        {                                                                                   \
            assert(false && "Value is not a " #NAME);                                       \
            return {};                                                                      \
        }                                                                                   \
    }                                                                                       \
    template<typename T, AttributeValueTraits Traits = AttributeValueTraitsFor<T>>          \
    inline TYPE attr_get_##NAME(const T& value)                                             \
        requires std::is_void_v<typename Traits::ReadContext>                               \
    {                                                                                       \
        return attr_get_##NAME<T, Traits>(value, empty{});                                  \
    }

ATTR_READ_IMPL(bool, bool, kBoolean);
ATTR_READ_IMPL(number, double, kNumber);
ATTR_READ_IMPL(uint64, uint64_t, kUint64);
ATTR_READ_IMPL(int64, int64_t, kInt64);
ATTR_READ_IMPL(string, std::string_view, kString);

#define ATTR_WRITE_IMPL_(FN_NAME, TYPE)                                             \
    template<typename T, AttributeValueTraits Traits = AttributeValueTraitsFor<T>>  \
    T FN_NAME(TYPE, [[maybe_unused]] devoid_t<typename Traits::WriteContext>& ctx); \
    template<typename T, AttributeValueTraits Traits = AttributeValueTraitsFor<T>>  \
    inline T FN_NAME(TYPE)                                                          \
        requires std::is_void_v<typename Traits::WriteContext>                      \
    {                                                                               \
        return FN_NAME<T, Traits>(value, empty{});                                  \
    }                                                                               \
    template<typename T, AttributeValueTraits Traits>                               \
    T FN_NAME(TYPE, [[maybe_unused]] devoid_t<typename Traits::WriteContext>& ctx)

#define ATTR_FROM_IMPL(TYPE) ATTR_WRITE_IMPL_(attr_from, TYPE)

ATTR_FROM_IMPL(bool value)
{
    return Traits::from_bool(value);
}

ATTR_FROM_IMPL(float value)
{
    return Traits::from_number((double)value);
}

ATTR_FROM_IMPL(double value)
{
    return Traits::from_number(value);
}

ATTR_FROM_IMPL(int32_t value)
{
    return Traits::from_number((double)value);
}

ATTR_FROM_IMPL(uint32_t value)
{
    return Traits::from_number((double)value);
}

ATTR_FROM_IMPL(uint64_t value)
{
    switch (store_as(value))
    {
        case AttributeValueType::kNumber: return Traits::from_number((double)value);
        case AttributeValueType::kUint64:
            return Traits::from_uint64(hrz::unsafe("value is a safe integer"), value, ctx);
        default: assert(false && "Unhandled case"); return attr_null<T, Traits>();
    }
}

ATTR_FROM_IMPL(int64_t value)
{
    switch (store_as(value))
    {
        case AttributeValueType::kNumber: return Traits::from_number((double)value);
        case AttributeValueType::kUint64:
            return Traits::from_uint64(hrz::unsafe("value is a safe integer"), value, ctx);
        case AttributeValueType::kInt64:
            return Traits::from_int64(hrz::unsafe("value is a safe integer"), value, ctx);
        default: assert(false && "Unhandled case"); return attr_null<T, Traits>();
    }
}

ATTR_FROM_IMPL(std::string_view value)
{
    return Traits::from_string(value, ctx);
}

ATTR_FROM_IMPL(const char* value)
{
    return Traits::from_string(value, ctx);
}

ATTR_FROM_IMPL(const RefAttributeValueTraits::Type& value)
{
    return std::visit(
        hrz::overload{
            [](std::nullptr_t) -> T { return attr_null<T, Traits>(); },
            [&ctx]<typename U>(const U& arg) -> T
            {
                using ArgType = std::decay_t<decltype(arg)>;
                static_assert(
                    hrz::is_one_of<ArgType, bool, double, uint64_t, int64_t, std::string_view>);
                return attr_from<T, Traits>(arg, ctx);
            }
        },
        value);
}

ATTR_FROM_IMPL(const std::string& value)
{
    return Traits::from_string(value, ctx);
}

#define ATTR_FROM_COLOR_IMPL(TYPE) ATTR_WRITE_IMPL_(attr_from_color, TYPE)

ATTR_FROM_COLOR_IMPL(const lm::vec4& value)
{
    return Traits::from_number((double)convert_rgba_color_to_uint(value));
}

ATTR_FROM_COLOR_IMPL(const lm::ubvec4& value)
{
    return Traits::from_number((double)std::bit_cast<uint32_t>(value));
}

#define ATTR_AS_IMPL(NAME, TYPE)                                                            \
    template<typename T, AttributeValueTraits Traits = AttributeValueTraitsFor<T>>          \
    TYPE attr_as_##NAME(const T& value, const devoid_t<typename Traits::ReadContext>& ctx); \
    template<typename T, AttributeValueTraits Traits = AttributeValueTraitsFor<T>>          \
    inline TYPE attr_as_##NAME(const T& value)                                              \
        requires std::is_void_v<typename Traits::ReadContext>                               \
    {                                                                                       \
        return attr_as_##NAME<T, Traits>(value, empty{});                                   \
    }                                                                                       \
    template<typename T, AttributeValueTraits Traits>                                       \
    TYPE attr_as_##NAME(const T& value, const devoid_t<typename Traits::ReadContext>& ctx)

ATTR_AS_IMPL(number, double)
{
    switch (attr_type<T, Traits>(value))
    {
        case AttributeValueType::kNull: return 0.0;
        case AttributeValueType::kBoolean:
            return Traits::get_bool(unsafe("type has been checked"), value, ctx) ? 1.0 : 0.0;
        case AttributeValueType::kNumber:
            return Traits::get_number(unsafe("type has been checked"), value, ctx);
        case AttributeValueType::kUint64:
            return (double)Traits::get_uint64(unsafe("type has been checked"), value, ctx);
        case AttributeValueType::kInt64:
            return (double)Traits::get_int64(unsafe("type has been checked"), value, ctx);
        default: return std::numeric_limits<double>::quiet_NaN();
    }
}

ATTR_AS_IMPL(uint64, uint64_t)
{
    switch (attr_type<T, Traits>(value))
    {
        case AttributeValueType::kNull: return 0;
        case AttributeValueType::kBoolean:
            return Traits::get_bool(unsafe("type has been checked"), value, ctx) ? 1 : 0;
        case AttributeValueType::kNumber:
            return (uint64_t)Traits::get_number(unsafe("type has been checked"), value, ctx);
        case AttributeValueType::kUint64:
            return Traits::get_uint64(unsafe("type has been checked"), value, ctx);
        case AttributeValueType::kInt64:
            return (uint64_t)Traits::get_int64(unsafe("type has been checked"), value, ctx);
        default: return 0;
    }
}

ATTR_AS_IMPL(int64, int64_t)
{
    switch (attr_type<T, Traits>(value))
    {
        case AttributeValueType::kNull: return 0;
        case AttributeValueType::kBoolean:
            return Traits::get_bool(unsafe("type has been checked"), value, ctx) ? 1 : 0;
        case AttributeValueType::kNumber:
            return (int64_t)Traits::get_number(unsafe("type has been checked"), value, ctx);
        case AttributeValueType::kUint64:
            return (int64_t)Traits::get_uint64(unsafe("type has been checked"), value, ctx);
        case AttributeValueType::kInt64:
            return Traits::get_int64(unsafe("type has been checked"), value, ctx);
        default: return 0;
    }
}

ATTR_AS_IMPL(string, std::string_view)
{
    switch (attr_type<T, Traits>(value))
    {
        case AttributeValueType::kNull:
        case AttributeValueType::kBoolean:
        case AttributeValueType::kNumber:
        case AttributeValueType::kUint64:
        case AttributeValueType::kInt64: return {};
        case AttributeValueType::kString:
            return Traits::get_string(unsafe("type has been checked"), value, ctx);
        default: return {};
    }
}

ATTR_AS_IMPL(bool, bool)
{
    switch (attr_type<T, Traits>(value))
    {
        case AttributeValueType::kNull: return false;
        case AttributeValueType::kBoolean:
            return Traits::get_bool(unsafe("type has been checked"), value, ctx);
        case AttributeValueType::kNumber:
        {
            const double num = Traits::get_number(unsafe("type has been checked"), value, ctx);
            return num != 0.0 && !std::isnan(num);
        }
        case AttributeValueType::kUint64:
            return Traits::get_uint64(unsafe("type has been checked"), value, ctx) != 0;
        case AttributeValueType::kInt64:
            return Traits::get_int64(unsafe("type has been checked"), value, ctx) != 0;
        case AttributeValueType::kString:
            return !Traits::get_string(unsafe("type has been checked"), value, ctx).empty();
        default: return false;
    }
}

ATTR_AS_IMPL(color, lm::ubvec4)
{
    return convert_uint_color_to_bytes((uint32_t)attr_as_uint64<T, Traits>(value, ctx));
}

#undef ATTR_AS_IMPL
#undef ATTR_FROM_IMPL
#undef ATTR_FROM_COLOR_IMPL
#undef ATTR_READ_IMPL
#undef ATTR_WRITE_IMPL_

} // namespace hrz::vector_data
