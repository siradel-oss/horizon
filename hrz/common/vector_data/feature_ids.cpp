#include "hrz/common/vector_data/feature_ids.h"

#include "hrz/common/profiling.h"
#include "hrz/common/vector_data/attribute_types_all.h"
#include "hrz/common/vector_data/attributes_ops.h"
#include "hrz/fnd/hash.h"
#include "hrz/fnd/log.h"

#include <algorithm>
#include <cassert>

namespace hrz::vector_data
{

void FeatureId::Builder::add_value(uint32_t attribute_id, OwnedAttributeValue value)
{
    for (size_t i = 0; i < _values.size(); ++i)
    {
        if (attribute_id == _values.at(i).attribute_id)
        {
            _values[i] = {attribute_id, std::move(value)};
            return;
        }
    }

    _values.push_back({attribute_id, std::move(value)});
}

FeatureId FeatureId::Builder::build()
{
    HRZ_SCOPED_SAMPLE("build feature id");

    FeatureId feature_id;
    feature_id._hash = 0;

    if (_values.empty())
    {
        return feature_id;
    }

    std::ranges::sort(
        _values, [](const Value& a, const Value& b) { return a.attribute_id < b.attribute_id; });

    for (const auto& value : _values)
    {
        uint64_t values_to_hash[] = {
            feature_id._hash, value.attribute_id, attr_hashed(value.value)};
        feature_id._hash = hrz::hash_mix<uint64_t>(values_to_hash);
    }

    feature_id._values = std::move(_values);
    return feature_id;
}

FeatureId FeatureId::from_proto(const hrz_proto::FeatureId& proto_id)
{
    Builder builder;

    for (const auto& proto_attribute : proto_id.attributes())
    {
        auto attribute_id = proto_attribute.id();
        auto proto_value = attr_from<OwnedAttributeValue>(attr_as_ref(proto_attribute.value()));
        builder.add_value(attribute_id, std::move(proto_value));
    }

    return builder.build();
}

void FeatureId::to_proto(hrz_proto::FeatureId* proto_id) const
{
    proto_id->mutable_attributes()->Reserve(_values.size());

    for (const auto& value : _values)
    {
        auto proto_attribute = proto_id->add_attributes();
        proto_attribute->set_id(value.attribute_id);
        *proto_attribute->mutable_value() = attr_from<ApiAttributeValue>(attr_as_ref(value.value));
    }
}

void FeatureIds::to_proto(google::protobuf::RepeatedPtrField<hrz_proto::FeatureId>* dst) const
{
    dst->Reserve(_size);

    hrz::InlinedVector<PackedAttributeValuesReader, 2> attribute_values_readers;
    for (const auto& attribute : _values)
    {
        attribute_values_readers.push_back(attribute.get_reader());
    }

    for (size_t row = 0; row < _size; ++row)
    {
        auto proto_feature_id = dst->Add();
        proto_feature_id->mutable_attributes()->Reserve(_values.size());

        for (size_t attr = 0; attr < _values.size(); ++attr)
        {
            const auto& attribute = _values.at(attr);

            auto proto_attribute = proto_feature_id->add_attributes();
            proto_attribute->set_id(attribute.attribute_id);
            *proto_attribute->mutable_value() =
                attr_from<ApiAttributeValue>(attribute_values_readers.at(attr).as_ref(row));
        }
    }
}

std::optional<FeatureIds> FeatureIds::make(
    std::span<AttributeValues> attribute_values,
    BlobArray<FeatureIdHash> hashes)
{
    HRZ_SCOPED_SAMPLE("make feature ids");

    if (attribute_values.empty())
    {
        {
            auto hashes_data = hashes.get_mutable_data();
            for (size_t i = 0; i < hashes_data.size(); ++i)
            {
                hashes_data[i] = 0;
            }
        }

        FeatureIds feature_ids;
        feature_ids._size = hashes.size();
        feature_ids._hashes = std::move(hashes);
        return {feature_ids};
    }

    auto value_count = attribute_values[0].values.size();

    for (size_t i = 1; i < attribute_values.size(); ++i)
    {
        if (attribute_values[i].values.size() != value_count)
        {
            HRZ_LOG_ERROR("Cannot make feature IDs with inconsistent value counts");
            return std::nullopt;
        }
    }

    for (size_t i = 0; i < attribute_values.size() - 1; ++i)
    {
        auto attribute_id = attribute_values[i].attribute_id;

        for (size_t j = i + 1; j < attribute_values.size(); ++j)
        {
            if (attribute_values[j].attribute_id == attribute_id)
            {
                HRZ_LOG_ERROR("Cannot make attribute values with duplicate attributes");
                return std::nullopt;
            }
        }
    }

    if (hashes.size() != value_count)
    {
        HRZ_LOG_ERROR("Expected {} entries in the hash array, got {}", value_count, hashes.size());
        return std::nullopt;
    }

    FeatureIds feature_ids;
    feature_ids._size = value_count;

    for (size_t i = 0; i < attribute_values.size(); ++i)
    {
        feature_ids._values.push_back(attribute_values[i]);
    }

    std::ranges::sort(
        feature_ids._values,
        [](const AttributeValues& a, const AttributeValues& b)
        { return a.attribute_id < b.attribute_id; });

    hrz::InlinedVector<PackedAttributeValuesReader, 2> attribute_values_readers;
    for (const auto& attribute : feature_ids._values)
    {
        attribute_values_readers.push_back(attribute.get_reader());
    }

    {
        auto hashes_data = hashes.get_mutable_data();

        for (size_t row = 0; row < feature_ids._size; ++row)
        {
            uint64_t feature_hash = 0;

            for (size_t attr = 0; attr < feature_ids._values.size(); ++attr)
            {
                const uint64_t value_hash = attr_hashed(attribute_values_readers[attr].as_ref(row));
                uint64_t data_to_hash[] = {
                    feature_hash, feature_ids._values[attr].attribute_id, value_hash};
                feature_hash = hrz::hash_mix<uint64_t>(data_to_hash);
            }

            hashes_data.at(row) = feature_hash;
        }
    }

    feature_ids._hashes = std::move(hashes);

    return {feature_ids};
}

FeatureIds::Hash FeatureIds::compute_hash()
{
    if (_hash.has_value())
    {
        return _hash.value();
    }

    auto hash = (uint64_t)_size;

    for (size_t attr = 0; attr < _values.size(); ++attr)
    {
        const auto& attribute = _values.at(attr);
        auto reader = attribute.get_reader();

        uint64_t values_hash = 0;
        for (size_t i = 0; i < attribute.values.size(); ++i)
        {
            values_hash = hrz::hash_mix<uint64_t>(values_hash, attr_hashed(reader.as_ref(i)));
        }

        uint64_t hashes[] = {hash, (uint64_t)attribute.attribute_id, values_hash};
        hash = hrz::hash_mix<uint64_t>(hashes);
    }

    _hash = {hash};
    return _hash.value();
}

size_t FeatureIds::size_bytes() const
{
    size_t size = sizeof(FeatureIds);

    for (const auto& attribute : _values)
    {
        size += sizeof(AttributeValues) + attribute.values.size_bytes()
            + attribute.out_of_line_data.size_bytes();
    }

    size += _hashes.size_bytes();

    return size;
}

bool FeatureIds::has_attribute(uint32_t attribute_id) const
{
    for (const auto& attribute : _values)
    {
        if (attribute.attribute_id == attribute_id)
        {
            return true;
        }
    }

    return false;
}

bool FeatureIds::has_same_attributes(const FeatureIds& feature_ids) const
{
    if (feature_ids._values.size() != _values.size()) return false;

    for (size_t i = 0; i < _values.size(); ++i)
    {
        if (feature_ids._values.at(i).attribute_id != _values.at(i).attribute_id)
        {
            return false;
        }
    }

    return true;
}

bool FeatureIds::contains(const FeatureId& feature_id) const
{
    if (feature_id.value_count() != _values.size()) return false;
    if (feature_id.value_count() == 0) return true;
    if (_size == 0) return false;

    for (size_t attr = 0; attr < _values.size(); ++attr)
    {
        if (feature_id.value_at(attr).attribute_id != _values.at(attr).attribute_id)
        {
            return false;
        }
    }

    hrz::InlinedVector<PackedAttributeValuesReader, 2> attribute_values_readers;
    for (const auto& attribute : _values)
    {
        attribute_values_readers.push_back(attribute.get_reader());
    }

    for (size_t row = 0; row < _size; ++row)
    {
        bool equal = true;

        for (size_t attr = 0; attr < _values.size(); ++attr)
        {
            const auto& value_a = feature_id.value_at(attr).value;
            const auto& value_b = attribute_values_readers.at(attr).as_ref(row);

            if (attr_type(value_a) != attr_type(value_b))
            {
                equal = false;
                break;
            }

            switch (attr_type(value_a))
            {
                case hrz::vector_data::AttributeValueType::kNull: equal = true; break;
                case hrz::vector_data::AttributeValueType::kBoolean:
                    equal = attr_get_bool(value_a) == attr_get_bool(value_b);
                    break;
                case hrz::vector_data::AttributeValueType::kNumber:
                    equal = attr_get_number(value_a) == attr_get_number(value_b);
                    break;
                case hrz::vector_data::AttributeValueType::kUint64:
                    equal = attr_get_uint64(value_a) == attr_get_uint64(value_b);
                    break;
                case hrz::vector_data::AttributeValueType::kInt64:
                    equal = attr_get_int64(value_a) == attr_get_int64(value_b);
                    break;
                case hrz::vector_data::AttributeValueType::kString:
                    equal = attr_get_string(value_a) == attr_get_string(value_b);
                    break;
                default: assert(false && "Unhandled case"); return false;
            }

            if (!equal)
            {
                break;
            }
        }

        if (equal) return true;
    }

    return false;
}

FeatureId FeatureIds::at(size_t i) const
{
    assert(i < _size);

    FeatureId feature_id;
    // @Safety the value is a primitive type that is copied, no reference to the blob is kept.
    feature_id._hash = _hashes.get_data().unsafe_at(i);

    for (const auto& attribute : _values)
    {
        FeatureId::Value feature_value;
        feature_value.attribute_id = attribute.attribute_id;
        feature_value.value = attr_from<OwnedAttributeValue>(attribute.get_reader().as_ref(i));

        feature_id._values.push_back(std::move(feature_value));
    }

    return feature_id;
}

} // namespace hrz::vector_data
