#pragma once

#include "hrz/common/color.h"
#include "hrz/common/vector_data/attribute_type_owned.h"
#include "hrz/common/vector_data/attribute_type_ref.h"
#include "hrz/common/vector_data/attribute_type_ref_arena.h"
#include "hrz/common/vector_data/attribute_types_defs.h"
#include "hrz/fnd/meta.h"
#include "hrz/fnd/string_utils.h"
#include "hrz/protocol/attributes/transform.pb.h"

namespace hrz::vector_data
{

// These must match Mapbox's definition.
static constexpr std::string_view typeof_string = "string";
static constexpr std::string_view typeof_number = "number";
static constexpr std::string_view typeof_null = "null";
static constexpr std::string_view typeof_boolean = "boolean";

template<typename T, AttributeValueTraits Traits = AttributeValueTraitsFor<T>>
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

uint64_t attr_hashed_inner(const RefAttributeValue& value);

template<typename T, AttributeValueTraits Traits = AttributeValueTraitsFor<T>>
inline uint64_t attr_hashed(const T& value, const devoid_t<typename Traits::ReadContext>& ctx)
{
    return attr_hashed_inner(attr_as_ref(value, ctx));
}

template<typename T, AttributeValueTraits Traits = AttributeValueTraitsFor<T>>
inline uint64_t attr_hashed(const T& value)
    requires std::is_void_v<typename Traits::ReadContext>
{
    return attr_hashed_inner(attr_as_ref(value));
}

std::string_view attr_to_string_from_non_string(
    const RefAttributeValue& value,
    std::span<char> buffer);

template<typename T, AttributeValueTraits Traits = AttributeValueTraitsFor<T>>
inline T attr_to_string_from_string(
    const RefAttributeValue& value,
    devoid_t<typename Traits::WriteContext>& ctx)
{
    if constexpr (std::is_same_v<Traits, RefAttributeValueArenaTraits>)
    {
        // Optimization: Avoid copying if we do ref->ref.
        // attr_from with an arena would write the string to the arena.
        return value;
    }
    else
    {
        return attr_from<T, Traits>(attr_get_string(value), ctx);
    }
}

template<typename T, AttributeValueTraits Traits = AttributeValueTraitsFor<T>>
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
                    const auto double_value = str::parse_double(str);
                    const bool bool_value = double_value && double_value.value() != 0.0
                        && !std::isnan(double_value.value());
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

template<typename T, AttributeValueTraits Traits = AttributeValueTraitsFor<T>>
inline T attr_transform(hrz_proto::AttributeTransform transform, const RefAttributeValue& value)
    requires std::is_void_v<typename Traits::WriteContext>
{
    const empty ctx;
    return attr_transform<T, Traits>(transform, value, ctx);
}

inline std::string attr_to_string(const RefAttributeValue& value)
{
    return std::get<std::string>(
        attr_transform<OwnedAttributeValue>(hrz_proto::ATTRIBUTE_TRANSFORM_TO_STRING, value));
}

} // namespace hrz::vector_data
