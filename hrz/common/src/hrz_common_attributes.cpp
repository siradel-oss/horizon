#include "hrz_common_attributes.h"

#include <hrz_fnd_hash.h>
#include <hrz_fnd_log.h>

#include <fmt/format.h>
#include <rapidjson/document.h>

#include <cstddef>

namespace hrz::vector_data
{
RefAttributeValueTraits::Type OwnedAttributeValueTraits::as_ref(const Type& value, empty)
{
    return std::visit(
        [](const auto& arg) -> RefAttributeValueTraits::Type
        {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::nullptr_t>)
            {
                return attr_null<RefAttributeValue>();
            }
            else if constexpr (
                std::is_same_v<T, bool> || std::is_same_v<T, double> || std::is_same_v<T, uint64_t>
                || std::is_same_v<T, int64_t> || std::is_same_v<T, std::string>)
            {
                return attr_from<RefAttributeValue>(arg);
            }
            else
            {
                static_assert(hrz::always_false<T>, "Unhandled case");
            }
        },
        value);
}

RefAttributeValueTraits::Type ApiAttributeValueTraits::as_ref(const Type& value, empty)
{
    switch (value.value_case())
    {
        case hrz_proto::AttributeValue::VALUE_NOT_SET: return attr_null<RefAttributeValue>();
        case hrz_proto::AttributeValue::kBooleanValue:
            return attr_from<RefAttributeValue>(value.boolean_value());
        case hrz_proto::AttributeValue::kNumberValue:
            return attr_from<RefAttributeValue>(value.number_value());
        case hrz_proto::AttributeValue::kUint64Value:
            return attr_from<RefAttributeValue>(value.uint64_value());
        case hrz_proto::AttributeValue::kInt64Value:
            return attr_from<RefAttributeValue>(value.int64_value());
        case hrz_proto::AttributeValue::kStringValue:
            return attr_from<RefAttributeValue>(value.string_value());
        default: assert(false && "Unhandled case"); return attr_null<RefAttributeValue>();
    }
}

RefAttributeValueTraits::Type InMemoryAttributeValueTraits::as_ref(const Type& value, empty)
{
    if (value.has_number_value()) return attr_from<RefAttributeValue>(value.number_value());
    if (value.has_boolean_value()) return attr_from<RefAttributeValue>(value.boolean_value());
    if (value.has_uint64_value()) return attr_from<RefAttributeValue>(value.uint64_value());
    if (value.has_int64_value()) return attr_from<RefAttributeValue>(value.int64_value());
    if (value.has_string_value()) return attr_from<RefAttributeValue>(value.string_value());
    return attr_null<RefAttributeValue>();
}

PackedAttributeValue PackedAttributeValuesEncoder::encode(uint64_t value)
{
    switch (store_as(value))
    {
        case AttributeValueType::kNumber:
            return attr_from<PackedAttributeValue>((double)value, _out_of_line_data);
        case AttributeValueType::kUint64:
        {
            auto it = _encoded_uint64s.find(value);
            if (it == _encoded_uint64s.end())
            {
                auto encoded = attr_from<PackedAttributeValue>(value, _out_of_line_data);
                _encoded_uint64s.emplace(value, encoded);
                return encoded;
            }
            else
            {
                return it->second;
            }
        }
        default: assert(false && "Unhandled case"); return attr_null<PackedAttributeValue>();
    }
}

PackedAttributeValue PackedAttributeValuesEncoder::encode(int64_t value)
{
    switch (store_as(value))
    {
        case AttributeValueType::kNumber:
            return attr_from<PackedAttributeValue>((double)value, _out_of_line_data);
        case AttributeValueType::kUint64:
        {
            auto it = _encoded_uint64s.find((uint64_t)value);
            if (it == _encoded_uint64s.end())
            {
                auto encoded = attr_from<PackedAttributeValue>((uint64_t)value, _out_of_line_data);
                _encoded_uint64s.emplace(value, encoded);
                return encoded;
            }
            else
            {
                return it->second;
            }
        }
        case AttributeValueType::kInt64:
        {
            auto it = _encoded_int64s.find(value);
            if (it == _encoded_int64s.end())
            {
                auto encoded = attr_from<PackedAttributeValue>(value, _out_of_line_data);
                _encoded_int64s.emplace(value, encoded);
                return encoded;
            }
            else
            {
                return it->second;
            }
        }
        default: assert(false && "Unhandled case"); return attr_null<PackedAttributeValue>();
    }
}

PackedAttributeValue PackedAttributeValuesEncoder::encode(std::string_view value)
{
    auto it = _encoded_strings.find(value);
    if (it == _encoded_strings.end())
    {
        auto encoded = attr_from<PackedAttributeValue>(value, _out_of_line_data);
        _encoded_strings.emplace(value, encoded);
        return encoded;
    }
    else
    {
        return it->second;
    }
}

std::optional<hrz::BlobArray<char>> PackedAttributeValuesEncoder::finalize()
{
    return _out_of_line_data.to_blob_array();
}

bool AttributeValuesBuilder::push_json(
    hrz_proto::AttributeTransform transform,
    const rapidjson::Value& value)
{
    RefAttributeValue ref_value = attr_null<RefAttributeValue>();
    bool handled = false;

    switch (value.GetType())
    {
        case rapidjson::kFalseType:
        case rapidjson::kTrueType:
        {
            ref_value = attr_from<RefAttributeValue>(value.GetBool());
            handled = true;
            break;
        }
        case rapidjson::kStringType:
        {
            ref_value = attr_from<RefAttributeValue>(
                std::string_view(value.GetString(), value.GetStringLength()));
            handled = true;
            break;
        }
        case rapidjson::kNumberType:
        {
            ref_value = attr_from<RefAttributeValue>(value.GetDouble());
            handled = true;
            break;
        }
        case rapidjson::kNullType:
        {
            ref_value = attr_null<RefAttributeValue>();
            handled = true;
            break;
        }
        case rapidjson::kArrayType:
        case rapidjson::kObjectType:
        default:
        {
            handled = false;
            break;
        }
    }

    push_transform(transform, ref_value);
    return handled;
}

std::optional<hrz::vector_data::AttributeValues> AttributeValuesBuilder::finalize(
    uint32_t attribute_id)
{
    auto values_opt = _values.to_blob_array();
    auto out_of_line_data_opt = _encoder.finalize();

    if (!values_opt.has_value() || !out_of_line_data_opt.has_value())
    {
        return std::nullopt;
    }

    return {{attribute_id, std::move(values_opt.value()), std::move(out_of_line_data_opt.value())}};
}

uint64_t attr_hashed_inner(const RefAttributeValue& value)
{
    switch (attr_type(value))
    {
        case AttributeValueType::kNull: return 0x90b5'fb0d'cc64'c209;
        case AttributeValueType::kBoolean:
            return RefAttributeValueTraits::get_bool(
                       unsafe{"type has been checked"}, value, empty{})
                ? 0x523e'f40f'1d36'6787
                : 0xb745'32e9'bc18'722e;
        case AttributeValueType::kNumber:
            return hrz::hash_mix<uint64_t>(
                0,
                std::bit_cast<uint64_t>(RefAttributeValueTraits::get_number(
                    unsafe{"type has been checked"}, value, empty{})));
        case AttributeValueType::kUint64:
            return hrz::hash_mix<uint64_t>(
                0,
                RefAttributeValueTraits::get_uint64(
                    unsafe{"type has been checked"}, value, empty{}));
        case AttributeValueType::kInt64:
            return hrz::hash_mix<uint64_t>(
                0,
                std::bit_cast<uint64_t>(RefAttributeValueTraits::get_int64(
                    unsafe{"type has been checked"}, value, empty{})));
        case AttributeValueType::kString:
            return hrz::murmur3_x64_64(RefAttributeValueTraits::get_string(
                unsafe{"type has been checked"}, value, empty{}));
        default: assert(false && "Unhandled case"); return 0;
    }
}

std::string_view attr_to_string_from_non_string(
    const RefAttributeValue& value,
    std::span<char> buffer)
{
    switch (attr_type(value))
    {
        case AttributeValueType::kNull: return {};
        case AttributeValueType::kBoolean:
        {
            return RefAttributeValueTraits::get_bool(
                       unsafe{"type has been checked"}, value, empty{})
                ? "true"
                : "false";
        }
        case AttributeValueType::kNumber:
        {
            auto end = fmt::format_to_n(
                buffer.data(), buffer.size(), "{}",
                RefAttributeValueTraits::get_number(
                    unsafe{"type has been checked"}, value, empty{}));
            return std::string_view(buffer.data(), end.size);
        }
        case AttributeValueType::kInt64:
        {
            auto end = fmt::format_to_n(
                buffer.data(), buffer.size(), "{}",
                RefAttributeValueTraits::get_int64(
                    unsafe{"type has been checked"}, value, empty{}));
            return std::string_view(buffer.data(), end.size);
        }
        case AttributeValueType::kUint64:
        {
            auto end = fmt::format_to_n(
                buffer.data(), buffer.size(), "{}",
                RefAttributeValueTraits::get_uint64(
                    unsafe{"type has been checked"}, value, empty{}));
            return std::string_view(buffer.data(), end.size);
        }
        case AttributeValueType::kString:
            return RefAttributeValueTraits::get_string(
                unsafe{"type has been checked"}, value, empty{});
        default: assert(false && "Unhandled case"); return {};
    }
}

} // namespace hrz::vector_data
