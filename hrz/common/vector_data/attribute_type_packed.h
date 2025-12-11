#pragma once

#include "hrz/common/blob_vector.h"
#include "hrz/common/vector_data/attribute_type_ref.h"
#include "hrz/common/vector_data/attribute_types_defs.h"

#include <bit>
#include <cmath>
#include <optional>
#include <span>
#include <vector>

namespace hrz::vector_data
{

struct CharSpanWriter
{
    size_t offset = 0;
    std::span<char> span;

    explicit CharSpanWriter(std::span<char> span) : span(span) {}
};

namespace packed_helpers
{

inline std::optional<off_t> append_out_of_line_data(
    std::span<const std::byte> data,
    std::vector<char>& ctx)
{
    auto size_before = ctx.size();
    ctx.resize(size_before + data.size());
    std::memcpy(ctx.data() + size_before, data.data(), data.size());
    return (off_t)size_before;
}

inline std::optional<off_t> append_out_of_line_data(
    std::span<const std::byte> data,
    CharSpanWriter& ctx)
{
    if (ctx.offset + data.size() > ctx.span.size()) return std::nullopt;
    auto size_before = ctx.offset;
    ctx.offset += data.size();
    std::memcpy(ctx.span.data() + size_before, data.data(), data.size());
    return (off_t)size_before;
}

inline std::optional<off_t> append_out_of_line_data(
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

} // namespace packed_helpers

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

    static std::optional<off_t> append_out_of_line_string(std::string_view str, WriteContext& ctx)
    {
        return packed_helpers::append_out_of_line_data(
            std::as_bytes(std::span<const char>(str.data(), str.size())), ctx);
    }

    static std::optional<off_t> append_out_of_line_uint64(uint64_t value, WriteContext& ctx)
    {
        auto buffer = std::bit_cast<std::array<std::byte, 8>>(value);
        return packed_helpers::append_out_of_line_data(buffer, ctx);
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

    static RefAttributeValueTraits::Type as_ref(const Type& value, const ReadContext& ctx)
    {
        switch (type(value))
        {
            case AttributeValueType::kNull: return attr_null<RefAttributeValueTraits::Type>();
            case AttributeValueType::kBoolean:
                return attr_from<RefAttributeValueTraits::Type>(
                    get_bool(unsafe{"type has been checked"}, value, ctx));
            case AttributeValueType::kNumber:
                return attr_from<RefAttributeValueTraits::Type>(std::bit_cast<double>(value));
            case AttributeValueType::kUint64:
                return attr_from<RefAttributeValueTraits::Type>(
                    get_uint64(unsafe{"type has been checked"}, value, ctx));
            case AttributeValueType::kInt64:
                return attr_from<RefAttributeValueTraits::Type>(
                    get_int64(unsafe{"type has been checked"}, value, ctx));
            case AttributeValueType::kString:
                return attr_from<RefAttributeValueTraits::Type>(
                    get_string(unsafe{"type has been checked"}, value, ctx));
            default:
                assert(false && "Unhandled case");
                return attr_null<RefAttributeValueTraits::Type>();
        }
    }

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
        const size_t size = (value & kStringSizeMask) >> kStringSizeShift;
        const size_t offset = value & kStringOffsetMask;
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

template<>
struct AttributeValueToTraits<typename PackedAttributeValueTraits<>::Type>
{
    using Traits = PackedAttributeValueTraits<>;
};

using PackedAttributeValue = typename PackedAttributeValueTraits<>::Type;

// Returns the size of the out-of-line data required for storing this values as packed.
inline size_t get_packed_out_of_line_size(const RefAttributeValue& value)
{
    return PackedAttributeValueTraits<>::get_out_of_line_size(value);
}

} // namespace hrz::vector_data
