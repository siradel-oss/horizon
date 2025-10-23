#pragma once

#include "hrz_common_blob_allocator.h"
#include "hrz_common_blob_array.h"
#include "hrz_common_blob_vector.h"
#include "hrz_common_color.h"

#include <hrz_fnd_arena.h>
#include <hrz_fnd_flat_hash_map.h>
#include <hrz_fnd_meta.h>
#include <hrz_fnd_string_utils.h>
#include <hrz_fnd_unsafe.h>
#include <hrz_protocol_all.h>

#include <rapidjson/fwd.h>

#include <bit>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>

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
// - ApiAttributeValue: A protobuf message that uses oneof. Used by API methods.
// - InMemoryAttributeValue: A protobuf message that uses optional. Used by the scene model.

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

// Here we define all the traits for each attribute value type.
// These traits define how values are read and stored.
// - Type is the storage type.
// - ReadContext is the context needed to read the value. Can be void if none is necessary.
// - WriteContext is the context needed to write the value. Can be void if none is necessary.
// All other methods are self-explanatory.

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

struct RefAttributeValueArenaTraits : public RefAttributeValueTraitsGeneric<hrz::Arena>
{
    static inline Type from_string(std::string_view value, hrz::Arena& arena)
    {
        return arena.str(value);
    }
};

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

struct CharSpanWriter
{
    size_t offset = 0;
    std::span<char> span;

    explicit CharSpanWriter(std::span<char> span) : span(span) {}
};

// This type stores attribute values of any type (int, uint, bool, double, string, color).
// All are stored as a double.
// All integers, bools and colors below kMaxSafeInteger are stored as is in the double.
// Other values are encoded in the NaN mantissa bits, and can be offsets in a data blob for
// strings and 64-bit integers.
// NaNs are encoded using the canonical qNaN bit pattern. (0x7ff8'0000'0000'0000)
// All special values are NaNs with the sign bit = 1.
//   - Bits[51:48] encode the type of payload.
//   - Type 1: 64-bit integer
//     - Bits[47:0] offset to 8-byte value
//   - Type 2: 64-bit unsigned integer
//     - Bits[47:0] offset to 8-byte value
//   - Type 3: String
//     - Bits[47:32] length
//     - Bits[31:0] offset
//   - Type 4: Boolean
//     - Bits[47:0] != 0 if true, false otherwise
//   - Type 5: Null

// WARNING!!!
// Be careful never to insert into a data blob using a RefAttributeValue that references a string in
// the same data blob. This can happen if you try to copy using from_ref(as_ref(packed)).
template<typename VectorChar = hrz::BlobVector<char>>
struct PackedAttributeValueTraits
{
    using Type = uint64_t;
    using ReadContext = std::span<const char>;
    using WriteContext = VectorChar;

    static constexpr uint64_t kStringMaxLength = 0x0000'ffff;

private:
    static constexpr uint64_t kNaN = 0x7ff8'0000'0000'0000;

    static constexpr uint64_t kExponentMask = 0x7ff0'0000'0000'0000;
    static constexpr uint64_t kMantissaMask = 0x000f'ffff'ffff'ffff;
    static constexpr uint64_t kSignMask = 0x8000'0000'0000'0000;

    static constexpr uint64_t kSpecialMask = kSignMask | kExponentMask;
    static constexpr uint64_t kSpecialTypeMask = 0x000f'0000'0000'0000;
    static constexpr uint64_t kSpecialTypeShift = 48;

    static constexpr uint64_t kType64BitInteger = 1;
    static constexpr uint64_t kType64BitUnsignedInteger = 2;
    static constexpr uint64_t kTypeString = 3;
    static constexpr uint64_t kTypeBoolean = 4;
    static constexpr uint64_t kTypeNull = 5;

    static constexpr uint64_t kSpecialPayloadMask = 0x0000'ffff'ffff'ffff;

    static constexpr uint64_t kIntegerOffsetMask = kSpecialPayloadMask;

    static constexpr uint64_t kStringSizeShift = 32;
    static constexpr uint64_t kStringSizeMask = kStringMaxLength << kStringSizeShift;
    static constexpr uint64_t kStringOffsetMask = 0x0000'0000'ffff'ffff;

    static_assert(
        (kStringSizeMask ^ kStringOffsetMask) == kSpecialPayloadMask,
        "String size and offset masks must be complementary");

    static constexpr Type from_special(uint64_t special_type, uint64_t payload)
    {
        return kSpecialMask | (special_type << kSpecialTypeShift) | payload;
    }

    constexpr static Type from_external_uint64(uint64_t offset)
    {
        return from_special(kType64BitUnsignedInteger, offset & kIntegerOffsetMask);
    }

    constexpr static Type from_external_int64(uint64_t offset)
    {
        return from_special(kType64BitInteger, offset & kIntegerOffsetMask);
    }

    constexpr static Type from_external_string(uint64_t offset, uint64_t size)
    {
        return from_special(
            kTypeString,
            ((size << kStringSizeShift) & kStringSizeMask) | (offset & kStringOffsetMask));
    }

    static std::optional<off_t> append_out_of_line_data(
        std::span<const std::byte> data,
        std::vector<char>& ctx)
    {
        auto size_before = ctx.size();
        ctx.resize(size_before + data.size());
        std::memcpy(ctx.data() + size_before, data.data(), data.size());
        return (off_t)size_before;
    }

    static std::optional<off_t> append_out_of_line_data(
        std::span<const std::byte> data,
        CharSpanWriter& ctx)
    {
        if (ctx.offset + data.size() > ctx.span.size()) return std::nullopt;
        auto size_before = ctx.offset;
        ctx.offset += data.size();
        std::memcpy(ctx.span.data() + size_before, data.data(), data.size());
        return (off_t)size_before;
    }

    static std::optional<off_t> append_out_of_line_data(
        std::span<const std::byte> data,
        hrz::BlobVector<char>& ctx)
    {
        auto size_before_opt = ctx.size();
        if (!size_before_opt.has_value()) return std::nullopt;

        auto size_before = (off_t)size_before_opt.value();
        ctx.resize(size_before + data.size());

        auto out_of_line_data = ctx.data();
        if (!out_of_line_data.has_value()) return std::nullopt;

        std::memcpy(out_of_line_data->data() + size_before, data.data(), data.size());
        return size_before;
    }

    static std::optional<off_t> append_out_of_line_string(std::string_view str, WriteContext& ctx)
    {
        return append_out_of_line_data(
            std::as_bytes(std::span<const char>(str.data(), str.size())), ctx);
    }

    static std::optional<off_t> append_out_of_line_uint64(uint64_t value, WriteContext& ctx)
    {
        auto buffer = std::bit_cast<std::array<std::byte, 8>>(value);
        return append_out_of_line_data(buffer, ctx);
    }

public:
    static constexpr AttributeValueType type(const Type& value)
    {
        if ((value & kSpecialMask) != kSpecialMask || (value & kMantissaMask) == 0)
        {
            return AttributeValueType::kNumber;
        }

        switch ((value & kSpecialTypeMask) >> kSpecialTypeShift)
        {
            case kType64BitInteger: return AttributeValueType::kInt64;
            case kType64BitUnsignedInteger: return AttributeValueType::kUint64;
            case kTypeString: return AttributeValueType::kString;
            case kTypeBoolean: return AttributeValueType::kBoolean;
            case kTypeNull: return AttributeValueType::kNull;
            default: assert(false && "Unhandled case"); return AttributeValueType::kNull;
        }
    }

    static constexpr Type empty_string() { return from_external_string(0, 0); }

    static RefAttributeValueTraits::Type as_ref(const Type& value, const ReadContext& ctx);

    static inline Type null() { return from_special(kTypeNull, 0); }

    static inline Type from_bool(bool value) { return from_special(kTypeBoolean, value ? 1 : 0); }

    static inline Type from_number(double value)
    {
        return std::isnan(value) ? kNaN : std::bit_cast<uint64_t>(value);
    }

    // @Safety: value must not be a safe integer. Otherwise use from_number.
    static inline Type from_uint64(unsafe, uint64_t value, WriteContext& ctx)
    {
        auto offset = append_out_of_line_uint64(value, ctx);
        if (offset)
        {
            return from_external_uint64(offset.value());
        }
        else
        {
            // Allocation error, fallback!
            return from_number((double)value);
        }
    }

    // @Safety: value must not be a safe integer or positive. Otherwise use from_number or
    // from_uint64.
    static inline Type from_int64(unsafe, int64_t value, WriteContext& ctx)
    {
        auto offset = append_out_of_line_uint64(value, ctx);
        if (offset)
        {
            return from_external_int64(offset.value());
        }
        else
        {
            // Allocation error, fallback!
            return from_number((double)value);
        }
    }

    static inline Type from_string(std::string_view value, WriteContext& ctx)
    {
        auto offset = append_out_of_line_string(value, ctx);
        if (offset)
        {
            return from_external_string(offset.value(), value.size());
        }
        else
        {
            // Allocation error, fallback!
            return empty_string();
        }
    }

    // @Safety: type must be checked to be kBoolean
    static inline bool get_bool(unsafe, const Type& value, const ReadContext&)
    {
        return (value & kSpecialPayloadMask) != 0;
    }

    // @Safety: type must be checked to be kNumber
    static inline double get_number(unsafe, const Type& value, const ReadContext&)
    {
        return std::bit_cast<double>(value);
    }

    // @Safety: type must be checked to be kUint64
    static inline uint64_t get_uint64(unsafe, const Type& value, const ReadContext& data)
    {
        auto* buffer = (const std::array<char, 8>*)(data.data() + (value & kIntegerOffsetMask));
        return std::bit_cast<uint64_t>(*buffer);
    }

    // @Safety: type must be checked to be kInt64
    static inline int64_t get_int64(unsafe, const Type& value, const ReadContext& data)
    {
        auto* buffer = (const std::array<char, 8>*)(data.data() + (value & kIntegerOffsetMask));
        return std::bit_cast<int64_t>(*buffer);
    }

    // @Safety: type must be checked to be kString
    static inline std::string_view get_string(unsafe, const Type& value, const ReadContext& data)
    {
        size_t size = (value & kStringSizeMask) >> kStringSizeShift;
        size_t offset = value & kStringOffsetMask;
        return std::string_view{data.data() + offset, size};
    }

    static inline size_t get_out_of_line_size(const RefAttributeValueTraits::Type& value)
    {
        switch (RefAttributeValueTraits::type(value))
        {
            case AttributeValueType::kBoolean: return 0;
            case AttributeValueType::kNull: return 0;
            case AttributeValueType::kNumber: return 0;
            case AttributeValueType::kUint64: return 8;
            case AttributeValueType::kInt64: return 8;
            case AttributeValueType::kString:
                return RefAttributeValueTraits::get_string(
                           unsafe("Type is checked"), value, empty{})
                    .size();
            default: assert(false && "Unhandled case"); return 0;
        }
    }
};

struct ApiAttributeValueTraits
{
    using Type = hrz_proto::AttributeValue;
    using ReadContext = void;
    using WriteContext = void;

    static RefAttributeValueTraits::Type as_ref(const Type& value, empty);

    static AttributeValueType type(const Type& value)
    {
        switch (value.value_case())
        {
            case hrz_proto::AttributeValue::kBooleanValue: return AttributeValueType::kBoolean;
            case hrz_proto::AttributeValue::VALUE_NOT_SET: return AttributeValueType::kNull;
            case hrz_proto::AttributeValue::kNumberValue: return AttributeValueType::kNumber;
            case hrz_proto::AttributeValue::kUint64Value: return AttributeValueType::kUint64;
            case hrz_proto::AttributeValue::kInt64Value: return AttributeValueType::kInt64;
            case hrz_proto::AttributeValue::kStringValue: return AttributeValueType::kString;
            default: assert(false && "Unhandled case"); return AttributeValueType::kNull;
        }
    }

    static inline Type null() { return Type{}; }

    static inline Type from_bool(bool value)
    {
        Type wrapper;
        wrapper.set_boolean_value(value);
        return wrapper;
    }

    static inline Type from_number(double value)
    {
        Type wrapper;
        wrapper.set_number_value(value);
        return wrapper;
    }

    // @Safety: value must not be a safe integer. Otherwise use from_number.
    static inline Type from_uint64(unsafe, uint64_t value, empty)
    {
        Type wrapper;
        wrapper.set_uint64_value(value);
        return wrapper;
    }

    // @Safety: value must not be a safe integer or positive. Otherwise use from_number or
    // from_uint64.
    static inline Type from_int64(unsafe, int64_t value, empty)
    {
        Type wrapper;
        wrapper.set_int64_value(value);
        return wrapper;
    }

    static inline Type from_string(std::string_view value, empty)
    {
        Type wrapper;
        wrapper.set_string_value(std::string(value));
        return wrapper;
    }

    static inline Type empty_string()
    {
        Type wrapper;
        wrapper.set_string_value("");
        return wrapper;
    }

    // @Safety: type must be checked to be kBoolean
    static inline bool get_bool(unsafe, const Type& value, empty) { return value.boolean_value(); }

    // @Safety: type must be checked to be kNumber
    static inline double get_number(unsafe, const Type& value, empty)
    {
        return value.number_value();
    }

    // @Safety: type must be checked to be kUint64
    static inline uint64_t get_uint64(unsafe, const Type& value, empty)
    {
        return value.uint64_value();
    }

    // @Safety: type must be checked to be kInt64
    static inline int64_t get_int64(unsafe, const Type& value, empty)
    {
        return value.int64_value();
    }

    // @Safety: type must be checked to be kString
    static inline std::string_view get_string(unsafe, const Type& value, empty)
    {
        return value.string_value();
    }
};

struct InMemoryAttributeValueTraits
{
    using Type = hrz_proto::InMemoryAttributeValue;
    using ReadContext = void;
    using WriteContext = void;

    static RefAttributeValueTraits::Type as_ref(const Type& value, empty);

    static inline Type empty_string()
    {
        Type wrapper;
        wrapper.set_string_value("");
        return wrapper;
    }

    static AttributeValueType type(const Type& value)
    {
        if (value.has_boolean_value()) return AttributeValueType::kBoolean;
        if (value.has_number_value()) return AttributeValueType::kNumber;
        if (value.has_uint64_value()) return AttributeValueType::kUint64;
        if (value.has_int64_value()) return AttributeValueType::kInt64;
        if (value.has_string_value()) return AttributeValueType::kString;
        return AttributeValueType::kNull;
    }

    static inline Type null() { return Type{}; }

    static inline Type from_bool(bool value)
    {
        Type wrapper;
        wrapper.set_boolean_value(value);
        return wrapper;
    }

    static inline Type from_number(double value)
    {
        Type wrapper;
        wrapper.set_number_value(value);
        return wrapper;
    }

    // @Safety: value must not be a safe integer. Otherwise use from_number.
    static inline Type from_uint64(unsafe, uint64_t value, empty)
    {
        Type wrapper;
        wrapper.set_uint64_value(value);
        return wrapper;
    }

    // @Safety: value must not be a safe integer or positive. Otherwise use from_number or
    // from_uint64.
    static inline Type from_int64(unsafe, int64_t value, empty)
    {
        Type wrapper;
        wrapper.set_int64_value(value);
        return wrapper;
    }

    static inline Type from_string(std::string_view value, empty)
    {
        Type wrapper;
        wrapper.set_string_value(std::string(value));
        return wrapper;
    }

    // @Safety: type must be checked to be kBoolean
    static inline bool get_bool(unsafe, const Type& value, empty) { return value.boolean_value(); }

    // @Safety: type must be checked to be kNumber
    static inline double get_number(unsafe, const Type& value, empty)
    {
        return value.number_value();
    }

    // @Safety: type must be checked to be kUint64
    static inline uint64_t get_uint64(unsafe, const Type& value, empty)
    {
        return value.uint64_value();
    }

    // @Safety: type must be checked to be kInt64
    static inline int64_t get_int64(unsafe, const Type& value, empty)
    {
        return value.int64_value();
    }

    // @Safety: type must be checked to be kString
    static inline std::string_view get_string(unsafe, const Type& value, empty)
    {
        return value.string_value();
    }
};

// AttributeValueToTraits is used to get the attribute storage trait from its
// values type automatically.

template<typename T>
struct AttributeValueToTraits
{
};

template<>
struct AttributeValueToTraits<typename RefAttributeValueTraits::Type>
{
    using Traits = RefAttributeValueTraits;
};

template<>
struct AttributeValueToTraits<typename OwnedAttributeValueTraits::Type>
{
    using Traits = OwnedAttributeValueTraits;
};

template<>
struct AttributeValueToTraits<typename PackedAttributeValueTraits<>::Type>
{
    using Traits = PackedAttributeValueTraits<>;
};

template<>
struct AttributeValueToTraits<typename ApiAttributeValueTraits::Type>
{
    using Traits = ApiAttributeValueTraits;
};

template<>
struct AttributeValueToTraits<typename InMemoryAttributeValueTraits::Type>
{
    using Traits = InMemoryAttributeValueTraits;
};

template<typename T>
using AttributeValueTraits =
    typename AttributeValueToTraits<std::remove_cv_t<std::remove_reference_t<T>>>::Traits;

using RefAttributeValue = typename RefAttributeValueTraits::Type;
using OwnedAttributeValue = typename OwnedAttributeValueTraits::Type;
using PackedAttributeValue = typename PackedAttributeValueTraits<>::Type;
using ApiAttributeValue = typename ApiAttributeValueTraits::Type;
using InMemoryAttributeValue = typename InMemoryAttributeValueTraits::Type;

// These first few methods don't require any read context. They are used to
// retrieve metadata about attribute values.

template<typename T, typename Traits = AttributeValueTraits<T>>
constexpr AttributeValueType attr_type(const T& value)
{
    return Traits::type(value);
}

// These must match Mapbox's definition.
static constexpr std::string_view typeof_string = "string";
static constexpr std::string_view typeof_number = "number";
static constexpr std::string_view typeof_null = "null";
static constexpr std::string_view typeof_boolean = "boolean";

template<typename T, typename Traits = AttributeValueTraits<T>>
constexpr std::string_view attr_mapbox_typeof(const T& value)
{
    switch (Traits::type(value))
    {
        case AttributeValueType::kNull: return typeof_null;
        case AttributeValueType::kBoolean: return typeof_boolean;
        case AttributeValueType::kNumber:
        case AttributeValueType::kUint64:
        case AttributeValueType::kInt64: return typeof_number;
        case AttributeValueType::kString: return typeof_string;
        default: assert(false && "Unhandled case"); return {};
    }
}

template<typename T, typename Traits = AttributeValueTraits<T>>
constexpr bool attr_is_null(const T& value)
{
    return Traits::type(value) == AttributeValueType::kNull;
}

template<typename T, typename Traits = AttributeValueTraits<T>>
constexpr bool attr_is_bool(const T& value)
{
    return Traits::type(value) == AttributeValueType::kBoolean;
}

template<typename T, typename Traits = AttributeValueTraits<T>>
constexpr bool attr_is_number(const T& value)
{
    return Traits::type(value) == AttributeValueType::kNumber;
}

template<typename T, typename Traits = AttributeValueTraits<T>>
constexpr bool attr_is_uint64(const T& value)
{
    return Traits::type(value) == AttributeValueType::kUint64;
}

template<typename T, typename Traits = AttributeValueTraits<T>>
constexpr bool attr_is_int64(const T& value)
{
    return Traits::type(value) == AttributeValueType::kInt64;
}

template<typename T, typename Traits = AttributeValueTraits<T>>
constexpr bool attr_is_64bit_integer(const T& value)
{
    auto type = Traits::type(value);
    return type == AttributeValueType::kUint64 || type == AttributeValueType::kInt64;
}

template<typename T, typename Traits = AttributeValueTraits<T>>
constexpr bool attr_is_string(const T& value)
{
    return Traits::type(value) == AttributeValueType::kString;
}

// Now we get into read and write methods. These methods do require context,
// hence the slight complication. This is because we want these methods to be
// usable on types that don't require context without having to provide a void
// or empty type every time. Hence why we need all the `requires` clauses.

template<typename T, typename Traits = AttributeValueTraits<T>>
inline T attr_null()
{
    return Traits::null();
}

template<typename T, typename Traits = AttributeValueTraits<T>>
inline T attr_empty_string()
{
    return Traits::empty_string();
}

template<typename T, typename Traits = AttributeValueTraits<T>>
RefAttributeValue attr_as_ref(const T& value, const devoid_t<typename Traits::ReadContext>& ctx)
{
    return Traits::as_ref(value, ctx);
}

template<typename T, typename Traits = AttributeValueTraits<T>>
inline RefAttributeValue attr_as_ref(const T& value)
    requires std::is_void_v<typename Traits::ReadContext>
{
    return Traits::as_ref(value, empty{});
}

#define ATTR_READ_IMPL(NAME, TYPE, ENUM_TYPE)                                               \
    template<typename T, typename Traits = AttributeValueTraits<T>>                         \
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
    template<typename T, typename Traits = AttributeValueTraits<T>>                         \
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

#define ATTR_AS_IMPL(NAME, TYPE)                                                            \
    template<typename T, typename Traits = AttributeValueTraits<T>>                         \
    TYPE attr_as_##NAME(const T& value, const devoid_t<typename Traits::ReadContext>& ctx); \
    template<typename T, typename Traits = AttributeValueTraits<T>>                         \
    inline TYPE attr_as_##NAME(const T& value)                                              \
        requires std::is_void_v<typename Traits::ReadContext>                               \
    {                                                                                       \
        return attr_as_##NAME<T, Traits>(value, empty{});                                   \
    }                                                                                       \
    template<typename T, typename Traits>                                                   \
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
            double num = Traits::get_number(unsafe("type has been checked"), value, ctx);
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

#define ATTR_WRITE_IMPL_(FN_NAME, TYPE)                                             \
    template<typename T, typename Traits = AttributeValueTraits<T>>                 \
    T FN_NAME(TYPE, [[maybe_unused]] devoid_t<typename Traits::WriteContext>& ctx); \
    template<typename T, typename Traits = AttributeValueTraits<T>>                 \
    inline T FN_NAME(TYPE)                                                          \
        requires std::is_void_v<typename Traits::WriteContext>                      \
    {                                                                               \
        return FN_NAME<T, Traits>(value, empty{});                                  \
    }                                                                               \
    template<typename T, typename Traits>                                           \
    T FN_NAME(TYPE, [[maybe_unused]] devoid_t<typename Traits::WriteContext>& ctx)

#define ATTR_FROM_IMPL(TYPE) ATTR_WRITE_IMPL_(attr_from, TYPE)
#define ATTR_FROM_COLOR_IMPL(TYPE) ATTR_WRITE_IMPL_(attr_from_color, TYPE)

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

ATTR_FROM_IMPL(const std::string& value)
{
    return Traits::from_string(value, ctx);
}

ATTR_FROM_IMPL(const RefAttributeValue& value)
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
            }},
        value);
}

ATTR_FROM_COLOR_IMPL(const lm::vec4& value)
{
    return Traits::from_number((double)convert_rgba_color_to_uint(value));
}

ATTR_FROM_COLOR_IMPL(const lm::ubvec4& value)
{
    return Traits::from_number((double)std::bit_cast<uint32_t>(value));
}

#undef ATTR_FROM_COLOR_IMPL
#undef ATTR_FROM_IMPL
#undef ATTR_WRITE_IMPL_
#undef ATTR_READ_IMPL
#undef ATTR_AS_IMPL

uint64_t attr_hashed_inner(const RefAttributeValue& value);

template<typename T, typename Traits = AttributeValueTraits<T>>
inline uint64_t attr_hashed(const T& value, const devoid_t<typename Traits::ReadContext>& ctx)
{
    return attr_hashed_inner(attr_as_ref(value, ctx));
}

template<typename T, typename Traits = AttributeValueTraits<T>>
inline uint64_t attr_hashed(const T& value)
    requires std::is_void_v<typename Traits::ReadContext>
{
    return attr_hashed_inner(attr_as_ref(value));
}

std::string_view attr_to_string_from_non_string(
    const RefAttributeValue& value,
    std::span<char> buffer);

template<typename T, typename Traits = AttributeValueTraits<T>>
inline T attr_to_string_from_string(
    const RefAttributeValue& value,
    devoid_t<typename Traits::WriteContext>& ctx)
{
    if constexpr (std::is_same_v<Traits, RefAttributeValueArenaTraits>)
    {
        // Optimization: Avoid copying if we do ref->ref.
        // Copying can happen if the write context is an arena.
        return value;
    }
    else
    {
        return attr_from<T, Traits>(attr_get_string(value), ctx);
    }
}

template<typename T, typename Traits = AttributeValueTraits<T>>
T attr_transform(
    hrz_proto::AttributeTransform transform,
    const RefAttributeValue& value,
    devoid_t<typename Traits::WriteContext>& ctx)
{
    switch (transform)
    {
        case hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_COLOR:
            if (attr_type(value) == AttributeValueType::kString)
            {
                return attr_from<T, Traits>(
                    parse_color_string(attr_get_string(value)).value_or(0), ctx);
            }
            else
            {
                return attr_from<T, Traits>((uint32_t)attr_as_uint64(value), ctx);
            }
            break;
        case hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_INT:
            if (attr_type(value) == AttributeValueType::kString)
            {
                return attr_from<T, Traits>(
                    str::parse_int64(attr_get_string(value)).value_or(0), ctx);
            }
            else
            {
                return attr_from<T, Traits>(attr_as_int64(value), ctx);
            }
            break;
        case hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_UINT:
            if (attr_type(value) == AttributeValueType::kString)
            {
                return attr_from<T, Traits>(
                    str::parse_uint64(attr_get_string(value)).value_or(0), ctx);
            }
            else
            {
                return attr_from<T, Traits>(attr_as_uint64(value), ctx);
            }
            break;
        case hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_NUMBER:
            if (attr_type(value) == AttributeValueType::kString)
            {
                return attr_from<T, Traits>(
                    str::parse_double(attr_get_string(value))
                        .value_or(std::numeric_limits<double>::quiet_NaN()),
                    ctx);
            }
            else
            {
                return attr_from<T, Traits>(attr_as_number(value), ctx);
            }
            break;
        case hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_BOOLEAN:
            if (attr_type(value) == AttributeValueType::kString)
            {
                auto str = str::trim_s(attr_get_string(value));
                if (str::iequals("true", str))
                {
                    return attr_from<T, Traits>(true, ctx);
                }
                else
                {
                    auto value = str::parse_double(str);
                    bool bool_value = value && *value != 0.0 && !std::isnan(*value);
                    return attr_from<T, Traits>(bool_value, ctx);
                }
            }
            else
            {
                return attr_from<T, Traits>(attr_as_bool(value), ctx);
            }
            break;
        case hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_TO_STRING:
        {
            if (attr_type(value) == AttributeValueType::kString)
            {
                return attr_to_string_from_string<T, Traits>(value, ctx);
            }
            else
            {
                char buffer[64];
                return attr_from<T, Traits>(attr_to_string_from_non_string(value, buffer), ctx);
            }
        }
        default: return attr_from<T, Traits>(value, ctx);
    }
}

template<typename T, typename Traits = AttributeValueTraits<T>>
inline T attr_transform(hrz_proto::AttributeTransform transform, const RefAttributeValue& value)
    requires std::is_void_v<typename Traits::WriteContext>
{
    empty ctx;
    return attr_transform<T, Traits>(transform, value, ctx);
}

inline std::string attr_to_string(const RefAttributeValue& value)
{
    return std::get<std::string>(
        attr_transform<OwnedAttributeValue>(hrz_proto::ATTRIBUTE_TRANSFORM_TO_STRING, value));
}

static_assert(
    sizeof(PackedAttributeValue) == sizeof(uint64_t),
    "PackedAttributeValue must be 64 bits");
static_assert(
    std::is_trivially_default_constructible_v<PackedAttributeValue>,
    "PackedAttributeValue must be trivial");

// Returns the size of the out-of-line data required for storing this values as packed.
inline size_t get_packed_out_of_line_size(const RefAttributeValue& value)
{
    return PackedAttributeValueTraits<>::get_out_of_line_size(value);
}

template<typename VectorChar>
RefAttributeValue PackedAttributeValueTraits<VectorChar>::as_ref(
    const Type& value,
    const ReadContext& ctx)
{
    switch (type(value))
    {
        case AttributeValueType::kNull: return attr_null<RefAttributeValue>();
        case AttributeValueType::kBoolean:
            return attr_from<RefAttributeValue>(
                get_bool(unsafe{"type has been checked"}, value, ctx));
        case AttributeValueType::kNumber:
            return attr_from<RefAttributeValue>(std::bit_cast<double>(value));
        case AttributeValueType::kUint64:
            return attr_from<RefAttributeValue>(
                get_uint64(unsafe{"type has been checked"}, value, ctx));
        case AttributeValueType::kInt64:
            return attr_from<RefAttributeValue>(
                get_int64(unsafe{"type has been checked"}, value, ctx));
        case AttributeValueType::kString:
            return attr_from<RefAttributeValue>(
                get_string(unsafe{"type has been checked"}, value, ctx));
        default: assert(false && "Unhandled case"); return attr_null<RefAttributeValue>();
    }
}

struct PackedAttributeValuesReader
{
    hrz::BlobArray<PackedAttributeValue>::Data values;
    hrz::BlobArray<char>::Data ool_data;

    inline size_t size() const { return values.size(); }

    inline bool as_bool(size_t i) const { return attr_as_bool(values[i], ool_data.as_span()); }

    inline int64_t as_int64(size_t i) const { return attr_as_int64(values[i], ool_data.as_span()); }

    inline uint64_t as_uint64(size_t i) const
    {
        return attr_as_uint64(values[i], ool_data.as_span());
    }

    inline double as_number(size_t i) const
    {
        return attr_as_number(values[i], ool_data.as_span());
    }

    inline std::string_view as_string(size_t i) const
    {
        return attr_as_string(values[i], ool_data.as_span());
    }

    inline lm::ubvec4 as_color(size_t i) const
    {
        return attr_as_color(values[i], ool_data.as_span());
    }

    inline RefAttributeValue as_ref(size_t i) const
    {
        return attr_as_ref(values[i], ool_data.as_span());
    }
};

struct AttributeValues
{
    PackedAttributeValuesReader get_reader() const
    {
        return {values.get_cdata(), out_of_line_data.get_cdata()};
    }

    uint32_t attribute_id{};

    hrz::BlobArray<PackedAttributeValue> values;

    // This is the blob of all packed strings and uint64s.
    hrz::BlobArray<char> out_of_line_data;
};

class PackedAttributeValuesEncoder
{
    hrz::BlobVector<char> _out_of_line_data;

    hrz::flat_hash_map<uint64_t, PackedAttributeValue> _encoded_uint64s;
    hrz::flat_hash_map<int64_t, PackedAttributeValue> _encoded_int64s;
    hrz::flat_hash_map<std::string, PackedAttributeValue> _encoded_strings;

public:
    PackedAttributeValuesEncoder(
        BlobAllocator* allocator,
        const monitoring::ResourceOwner& blob_owner) :
        _out_of_line_data(hrz::BlobVector<char>(allocator))
    {
        _out_of_line_data.register_blob_metadata(
            "contents"_ss, "attribute values out of line data"_ss);
        _out_of_line_data.register_blob_owner(blob_owner);
    }

    inline PackedAttributeValue encode(bool value)
    {
        return PackedAttributeValueTraits<>::from_bool(value);
    }

    PackedAttributeValue encode(uint64_t value);
    PackedAttributeValue encode(int64_t value);

    inline PackedAttributeValue encode(double value)
    {
        return PackedAttributeValueTraits<>::from_number(value);
    }

    PackedAttributeValue encode(std::string_view value);

    PackedAttributeValue encode(const char* value) { return encode(std::string_view(value)); }

    template<int N>
    PackedAttributeValue encode(const char (&value)[N])
    {
        return encode(std::string_view(value, N - 1));
    }

    PackedAttributeValue encode_ref(RefAttributeValue value)
    {
        switch (attr_type(value))
        {
            case AttributeValueType::kNull: return attr_null<PackedAttributeValue>();
            case AttributeValueType::kBoolean: return encode(attr_get_bool(value));
            case AttributeValueType::kNumber: return encode(attr_get_number(value));
            case AttributeValueType::kUint64: return encode(attr_get_uint64(value));
            case AttributeValueType::kInt64: return encode(attr_get_int64(value));
            case AttributeValueType::kString: return encode(attr_get_string(value));
            default: assert(false && "Unhandled case"); return attr_null<PackedAttributeValue>();
        }
    }

    std::optional<std::span<const char>> get_out_of_line_data() const
    {
        return _out_of_line_data.data();
    }

    std::optional<hrz::BlobArray<char>> finalize();
};

class AttributeValuesBuilder
{
    hrz::BlobVector<vector_data::PackedAttributeValue> _values;
    PackedAttributeValuesEncoder _encoder;

public:
    AttributeValuesBuilder(
        size_t initial_capacity,
        BlobAllocator* allocator,
        const monitoring::ResourceOwner& blob_owner) :
        _values(hrz::BlobVector<vector_data::PackedAttributeValue>(allocator, initial_capacity)),
        _encoder(allocator, blob_owner)
    {
        _values.register_blob_metadata("contents"_ss, "attribute values values"_ss);
        _values.register_blob_owner(blob_owner);
    }

    // This can be used to intern values that are known to be repeated.
    constexpr PackedAttributeValuesEncoder& encoder() { return _encoder; }

    inline void reserve(size_t capacity) { _values.reserve(capacity); }

    inline void resize(size_t size) { _values.resize(size); }

    template<typename T>
    void push(T&& value)
    {
        _values.push_back(_encoder.encode(std::forward<T>(value)));
    }

    // Returns whether the value type was handled. In any case a value is inserted.
    bool push_json(hrz_proto::AttributeTransform transform, const rapidjson::Value& value);

    void push_ref(const RefAttributeValue& value) { _values.push_back(_encoder.encode_ref(value)); }

    void push_transform(hrz_proto::AttributeTransform transform, const RefAttributeValue& value)
    {
        if (transform == hrz_proto::AttributeTransform::ATTRIBUTE_TRANSFORM_NONE)
        {
            push_ref(value);
        }
        else
        {
            auto value_transformed = attr_transform<OwnedAttributeValue>(transform, value);
            push_ref(attr_as_ref(value_transformed));
        }
    }

    // @Safety: the value must have been encoded using the encoder of this builder.
    void push_encoded(unsafe, PackedAttributeValue value) { _values.push_back(value); }

    void push_null() { _values.push_back(attr_null<PackedAttributeValue>()); }

    template<typename T>
    inline void set(size_t i, T&& value)
    {
        auto values_data = _values.data();
        if (values_data.has_value())
        {
            values_data.value()[i] = _encoder.encode(std::forward<T>(value));
        }
    }

    inline void set_ref(size_t i, const RefAttributeValue& value)
    {
        auto values_data = _values.data();
        if (values_data.has_value())
        {
            values_data.value()[i] = _encoder.encode_ref(value);
        }
    }

    std::optional<hrz::vector_data::AttributeValues> finalize(uint32_t attribute_id);
};

} // namespace hrz::vector_data
