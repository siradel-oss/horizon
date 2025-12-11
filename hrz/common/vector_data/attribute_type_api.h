#pragma once

#include "hrz/common/vector_data/attribute_types_defs.h"
#include "hrz/protocol/attributes/attribute.pb.h"

namespace hrz::vector_data
{

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

template<>
struct AttributeValueToTraits<typename ApiAttributeValueTraits::Type>
{
    using Traits = ApiAttributeValueTraits;
};

using ApiAttributeValue = typename ApiAttributeValueTraits::Type;

} // namespace hrz::vector_data
