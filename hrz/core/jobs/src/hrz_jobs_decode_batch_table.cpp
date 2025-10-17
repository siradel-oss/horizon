#include "hrz_jobs_declarations.h"

#include <hrz_common_attributes.h>
#include <hrz_common_profiling.h>
#include <hrz_common_three_d_tiles.h>
#include <hrz_fnd_array_view.h>
#include <hrz_fnd_flat_hash_set.h>
#include <hrz_fnd_inlined_vector.h>
#include <hrz_fnd_json_utils.h>
#include <hrz_fnd_log.h>

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

#include <bit>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace hrz_jobs::decode_three_d_tiles_batch_table
{
using AttributeComponentType = hrz::three_d_tiles::AttributeComponentType;
using AttributeValue = hrz::vector_data::PackedAttributeValue;
using AttributeValues = hrz::vector_data::AttributeValues;

namespace
{
static constexpr uint32_t DummyAttributeId = -1;

std::optional<uint8_t> component_count_from_string(std::string_view str)
{
    if (str == "SCALAR")
    {
        return 1;
    }
    else if (str == "VEC2")
    {
        return 2;
    }
    else if (str == "VEC3")
    {
        return 3;
    }
    else if (str == "VEC4")
    {
        return 4;
    }

    HRZ_LOG_ERROR("Invalid type: {}", str);
    return {};
}

size_t get_size_for_attribute_type(AttributeComponentType component_type, uint8_t component_count)
{
    assert(component_count >= 1 && component_count <= 4);

    size_t size = 0;
    switch (component_type)
    {
        case AttributeComponentType::BYTE:
        case AttributeComponentType::UNSIGNED_BYTE: size = 1; break;
        case AttributeComponentType::SHORT:
        case AttributeComponentType::UNSIGNED_SHORT: size = 2; break;
        case AttributeComponentType::INT:
        case AttributeComponentType::UNSIGNED_INT:
        case AttributeComponentType::FLOAT: size = 4; break;
        case AttributeComponentType::INT64:
        case AttributeComponentType::UNSIGNED_INT64:
        case AttributeComponentType::DOUBLE: size = 8; break;
        default: assert(false && "Unhandled case"); return 0;
    }

    return size * component_count;
}

bool attribute_type_is_compatible(uint8_t component_count)
{
    if (component_count != 1)
    {
        // @Todo Handle attribute with multiple components.
        // For example, VEC4s of UNSIGNED_BYTEs can be read as colours.
        return false;
    }
    return true;
}

template<typename FullLengthType, typename DataType>
std::optional<AttributeValues> _convert_attribute_binary_values(
    uint32_t attribute_id,
    hrz_proto::AttributeTransform transform,
    const std::byte* data,
    size_t byte_stride,
    size_t count,
    hrz::BlobAllocator* blob_allocator,
    const hrz::monitoring::ResourceOwner& resource_owner)
{
    static_assert(
        std::is_arithmetic_v<FullLengthType>,
        "Binary-packed attributes should be of arithmetic type");

    hrz::ArrayView<const DataType> view((const DataType*)data, count, byte_stride);
    hrz::vector_data::AttributeValuesBuilder builder(count, blob_allocator, resource_owner);

    if (transform == hrz_proto::ATTRIBUTE_TRANSFORM_NONE)
    {
        for (size_t i = 0; i < count; ++i)
        {
            builder.push((FullLengthType)view[i]);
        }
    }
    else
    {
        for (size_t i = 0; i < count; ++i)
        {
            auto ref_value = hrz::vector_data::attr_from<hrz::vector_data::RefAttributeValue>(
                (FullLengthType)view[i]);
            builder.push_transform(transform, ref_value);
        }
    }

    return builder.finalize(attribute_id);
}

std::optional<AttributeValues> _convert_attribute_binary_values(
    uint32_t attribute_id,
    hrz_proto::AttributeTransform transform,
    const std::byte* data,
    size_t byte_stride,
    AttributeComponentType component_type,
    size_t count,
    hrz::BlobAllocator* blob_allocator,
    const hrz::monitoring::ResourceOwner& resource_owner)
{
    switch (component_type)
    {
        case AttributeComponentType::BYTE:
            return _convert_attribute_binary_values<int64_t, int8_t>(
                attribute_id, transform, data, byte_stride, count, blob_allocator, resource_owner);
        case AttributeComponentType::UNSIGNED_BYTE:
            return _convert_attribute_binary_values<uint64_t, uint8_t>(
                attribute_id, transform, data, byte_stride, count, blob_allocator, resource_owner);
        case AttributeComponentType::SHORT:
            return _convert_attribute_binary_values<int64_t, int16_t>(
                attribute_id, transform, data, byte_stride, count, blob_allocator, resource_owner);
        case AttributeComponentType::UNSIGNED_SHORT:
            return _convert_attribute_binary_values<uint64_t, uint16_t>(
                attribute_id, transform, data, byte_stride, count, blob_allocator, resource_owner);
        case AttributeComponentType::INT:
            return _convert_attribute_binary_values<int64_t, int32_t>(
                attribute_id, transform, data, byte_stride, count, blob_allocator, resource_owner);
        case AttributeComponentType::UNSIGNED_INT:
            return _convert_attribute_binary_values<uint64_t, uint32_t>(
                attribute_id, transform, data, byte_stride, count, blob_allocator, resource_owner);
        case AttributeComponentType::FLOAT:
            return _convert_attribute_binary_values<double, float>(
                attribute_id, transform, data, byte_stride, count, blob_allocator, resource_owner);
        case AttributeComponentType::INT64:
            return _convert_attribute_binary_values<int64_t, int64_t>(
                attribute_id, transform, data, byte_stride, count, blob_allocator, resource_owner);
        case AttributeComponentType::UNSIGNED_INT64:
            return _convert_attribute_binary_values<uint64_t, uint64_t>(
                attribute_id, transform, data, byte_stride, count, blob_allocator, resource_owner);
        case AttributeComponentType::DOUBLE:
            return _convert_attribute_binary_values<double, double>(
                attribute_id, transform, data, byte_stride, count, blob_allocator, resource_owner);
        default:
        {
            assert(false && "Unhandled case");
            return std::nullopt;
        }
    }
}

std::optional<AttributeValues> _decode_attribute_values_from_binary(
    uint32_t attribute_id,
    hrz_proto::AttributeTransform transform,
    std::span<const std::byte> bin_data,
    size_t byte_offset,
    AttributeComponentType component_type,
    uint8_t component_count,
    size_t batch_count,
    hrz::BlobAllocator* blob_allocator,
    const hrz::monitoring::ResourceOwner& resource_owner)
{
    HRZ_SCOPED_SAMPLE("decode attribute values from binary");

    assert(component_count >= 1 && component_count <= 4);

    size_t stride = get_size_for_attribute_type(component_type, component_count);

    assert(stride * batch_count + byte_offset <= bin_data.size_bytes());

    // @Todo What to do with attributes with multiple components?
    return _convert_attribute_binary_values(
        attribute_id, transform, bin_data.data() + byte_offset, stride, component_type, batch_count,
        blob_allocator, resource_owner);
}

std::optional<hrz::vector_data::AttributeValues> _decode_attribute_values_array(
    std::string_view attribute_name,
    uint32_t attribute_id,
    hrz_proto::AttributeTransform transform,
    const rapidjson::Value& attribute_json_value,
    uint32_t expected_length,
    hrz::BlobAllocator* blob_allocator,
    const hrz::monitoring::ResourceOwner& resource_owner)
{
    // Values are in the JSON document
    if (attribute_json_value.Size() != expected_length)
    {
        HRZ_LOG_ERROR(
            "Wrong array length for attribute \"{}\": {} values for {} features", attribute_name,
            attribute_json_value.Size(), expected_length);
        return std::nullopt;
    }

    hrz::vector_data::AttributeValuesBuilder builder(
        expected_length, blob_allocator, resource_owner);
    bool has_unhandled_types = false;

    for (const auto& entry : attribute_json_value.GetArray())
    {
        has_unhandled_types = !builder.push_json(transform, entry) || has_unhandled_types;
    }

    if (has_unhandled_types)
    {
        HRZ_LOG_ERROR("Unhandled type for attribute \"{}\"", attribute_name);
        return std::nullopt;
    }

    return std::move(builder).finalize(attribute_id);
}

std::optional<AttributeValues> _decode_attribute_values_from_blob(
    std::string_view attribute_name,
    uint32_t attribute_id,
    hrz_proto::AttributeTransform transform,
    const rapidjson::Value& attribute_json_value,
    std::span<const std::byte> batch_table_bin_data,
    uint32_t expected_length,
    hrz::BlobAllocator* blob_allocator,
    const hrz::monitoring::ResourceOwner& resource_owner)
{
    if (attribute_json_value.HasMember("byteOffset")
        && attribute_json_value.HasMember("componentType")
        && attribute_json_value.HasMember("type"))
    {
        const auto& byte_offset_node = attribute_json_value["byteOffset"];
        const auto& component_type_node = attribute_json_value["componentType"];
        const auto& type_node = attribute_json_value["type"];

        if (byte_offset_node.IsInt() && component_type_node.IsString() && type_node.IsString())
        {
            size_t byte_offset = byte_offset_node.GetInt();
            auto component_type =
                hrz::three_d_tiles::component_type_from_string(component_type_node.GetString());
            auto component_count = component_count_from_string(type_node.GetString());

            if (component_type.has_value() && component_count.has_value()
                && component_count.value() >= 1 && component_count.value() <= 4
                && byte_offset
                        + get_size_for_attribute_type(
                              component_type.value(), component_count.value())
                            * expected_length
                    <= batch_table_bin_data.size()
                && attribute_type_is_compatible(*component_count))
            {
                return _decode_attribute_values_from_binary(
                    attribute_id, transform, batch_table_bin_data, byte_offset,
                    component_type.value(), component_count.has_value(), expected_length,
                    blob_allocator, resource_owner);
            }
        }
    }

    HRZ_LOG_ERROR("Invalid definition for attribute \"{}\"", attribute_name);
    return std::nullopt;
}

std::optional<AttributeValues> _decode_attribute_values(
    std::string_view attribute_name,
    uint32_t attribute_id,
    hrz_proto::AttributeTransform transform,
    const rapidjson::Value& attribute_json_value,
    std::span<const std::byte> batch_table_bin_data,
    uint32_t expected_length,
    hrz::BlobAllocator* blob_allocator,
    const hrz::monitoring::ResourceOwner& resource_owner)
{
    HRZ_SCOPED_SAMPLE("decode attribute values");

    if (attribute_json_value.IsArray())
    {
        return _decode_attribute_values_array(
            attribute_name, attribute_id, transform, attribute_json_value, expected_length,
            blob_allocator, resource_owner);
    }
    else if (attribute_json_value.IsObject())
    {
        return _decode_attribute_values_from_blob(
            attribute_name, attribute_id, transform, attribute_json_value, batch_table_bin_data,
            expected_length, blob_allocator, resource_owner);
    }
    else
    {
        return std::nullopt;
    }
}

std::optional<AttributeValues> _decode_u64_attribute_values(
    std::string_view attribute_name,
    uint32_t attribute_id,
    hrz_proto::AttributeTransform transform,
    const rapidjson::Value& attribute_json_value,
    std::span<const std::byte> batch_table_bin_data,
    uint32_t expected_length,
    hrz::BlobAllocator* blob_allocator,
    const hrz::monitoring::ResourceOwner& resource_owner)
{
    return _decode_attribute_values(
        attribute_name, attribute_id, transform, attribute_json_value, batch_table_bin_data,
        expected_length, blob_allocator, resource_owner);
}

struct BatchClass
{
    std::string name;
    uint32_t length{};

    struct Attribute
    {
        size_t index_in_tileset;
        AttributeValues values;

        // The reader is used to avoid recreating it repeatedly when reading the
        // batch table hierarchy and thus causing large amounts of locking and
        // unlocking in the blob allocator.
        hrz::vector_data::PackedAttributeValuesReader reader;
    };

    std::vector<Attribute> attributes;
};

bool _decode_batch_table_hierarchy(
    const uint32_t batch_length,
    const rapidjson::Value& bth_json,
    std::span<const std::byte> batch_table_bin_data,
    std::span<const hrz::three_d_tiles::AttributeConfig> attributes,
    std::span<std::optional<AttributeValues>> attribute_values,
    hrz::BlobAllocator* blob_allocator,
    const hrz::monitoring::ResourceOwner& resource_owner)
{
    HRZ_SCOPED_SAMPLE("decode batch table hierarchy");

    hrz::flat_hash_set<size_t> attributes_to_load;
    std::optional<size_t> class_id_attribute;
    std::optional<size_t> class_name_attribute;

    for (size_t i = 0; i < attributes.size(); ++i)
    {
        if (attribute_values[i].has_value()) continue;

        if (attributes[i].has_batch_class_id_source() && !class_id_attribute.has_value())
        {
            attributes_to_load.insert(i);
            class_id_attribute = i;
        }
        else if (attributes[i].has_batch_class_name_source() && !class_name_attribute.has_value())
        {
            attributes_to_load.insert(i);
            class_name_attribute = i;
        }
    }

    uint32_t instances_length = hrz::json::get_int_or(bth_json, "instancesLength", 0);
    if (instances_length < batch_length)
    {
        HRZ_LOG_ERROR("Batch table hierarchy doesn't have enough instances");
        return false;
    }

    const auto& classes_json = hrz::json::get_member_or_null(bth_json, "classes");
    if (classes_json.IsNull() || !classes_json.IsArray() || classes_json.GetArray().Size() == 0)
    {
        HRZ_LOG_WARNING("Batch table hierarchy doesn't have any class :(");
        return true;
    }

    std::vector<BatchClass> batch_classes;

    for (const auto& class_json : classes_json.GetArray())
    {
        BatchClass batch_class;
        batch_class.name = hrz::json::get_str_or(class_json, "name", "");
        batch_class.length = (uint32_t)hrz::json::get_int_or(class_json, "length", 0);

        for (size_t i = 0; i < attributes.size(); ++i)
        {
            // If the attribute is already loaded or comes from a vector data loader, skip it!
            if (attribute_values[i].has_value() || !attributes[i].has_batch_table_source())
                continue;

            const auto& attribute_json = hrz::json::get_nested_member_or_null(
                class_json, {"instances", attributes[i].name.c_str()});
            if (attribute_json.IsNull()) continue;

            auto attribute_values = _decode_attribute_values(
                attributes[i].name, DummyAttributeId, attributes[i].transform, attribute_json,
                batch_table_bin_data, batch_class.length, blob_allocator, resource_owner);

            if (!attribute_values || attribute_values->values.size() != batch_class.length)
                continue;

            auto reader = attribute_values->get_reader();
            batch_class.attributes.emplace_back(
                BatchClass::Attribute{i, std::move(*attribute_values), std::move(reader)});
            attributes_to_load.insert(i);
        }

        batch_classes.push_back(std::move(batch_class));
    }

    // "classIds is an array of integers of length instancesLength. Each
    // value specifies the instances's class as an index in the classes
    // array."

    hrz::vector_data::PackedAttributeValuesReader class_ids;
    {
        auto class_ids_opt = _decode_u64_attribute_values(
            "classIds", DummyAttributeId, hrz_proto::ATTRIBUTE_TRANSFORM_NONE,
            hrz::json::get_member_or_null(bth_json, "classIds"), batch_table_bin_data,
            instances_length, blob_allocator, resource_owner);
        if (!class_ids_opt)
        {
            HRZ_LOG_ERROR("classIds array couldn't be decoded");
            return false;
        }

        class_ids = class_ids_opt.value().get_reader();
    }

    if (class_ids.values.size() != instances_length)
    {
        HRZ_LOG_ERROR("classIds array has invalid length");
        return false;
    }

    {
        auto ool_span = class_ids.ool_data.as_span();
        uint64_t max_class_id = hrz::vector_data::attr_as_uint64(
            *std::max_element(
                class_ids.values.begin(), class_ids.values.end(),
                [ool_span](AttributeValue value_a, AttributeValue value_b)
                {
                    return hrz::vector_data::attr_as_uint64(value_a, ool_span)
                        < hrz::vector_data::attr_as_uint64(value_b, ool_span);
                }),
            ool_span);
        if (max_class_id >= batch_classes.size())
        {
            HRZ_LOG_ERROR("classIds contains invalid class ids");
            return false;
        }
    }

    // "The Batch Table Hierarchy does not directly provide an instances's
    // index into its class's instances array. Instead the index can be
    // inferred by the number of instances with the same classId that have
    // appeared before it. An implementation may want to compute these
    // indices at load time so that property access is as fast as
    // possible."

    std::vector<uint64_t> instance_indices;
    {
        std::vector<uint64_t> next_instance_index_per_class(batch_classes.size(), 0);
        instance_indices.reserve(instances_length);

        for (size_t i = 0; i < instances_length; ++i)
        {
            auto class_id = class_ids.as_uint64(i);
            auto instance_index = next_instance_index_per_class[class_id]++;
            instance_indices.push_back(instance_index);
        }

        for (size_t i = 0; i < batch_classes.size(); ++i)
        {
            if (next_instance_index_per_class[i] != batch_classes[i].length)
            {
                HRZ_LOG_ERROR(
                    "Wrong instance length for class {} (\"{}\")", i, batch_classes[i].name);
                return false;
            }
        }
    }

    // "parentCounts is an array of integers of length instancesLength.
    // Each value specifies the number of parents that instance has. If
    // omitted, parentCounts is implicitly an array of length
    // instancesLength, where all values are 1."

    hrz::vector_data::PackedAttributeValuesReader parent_counts;
    if (bth_json.HasMember("parentCounts"))
    {
        auto parent_counts_opt = _decode_u64_attribute_values(
            "parentCounts", DummyAttributeId, hrz_proto::ATTRIBUTE_TRANSFORM_NONE,
            hrz::json::get_member_or_null(bth_json, "parentCounts"), batch_table_bin_data,
            instances_length, blob_allocator, resource_owner);
        if (!parent_counts_opt.has_value())
        {
            HRZ_LOG_ERROR("parentCounts array couldn't be decoded");
            return false;
        }
        parent_counts = parent_counts_opt.value().get_reader();
    }

    auto get_parent_count = [&parent_counts,
                             instances_length]() -> std::function<uint64_t(uint32_t)>
    {
        if (parent_counts.values.size() == instances_length)
        {
            return [&parent_counts](uint32_t index) -> uint64_t
            { return parent_counts.as_uint64(index); };
        }
        else
        {
            return [](uint32_t) -> uint64_t { return 1; };
        }
    }();

    // Compute first parent indices

    size_t total_parent_count = 0;
    std::vector<uint64_t> first_parents;
    {
        uint64_t first_parent = 0;
        first_parents.reserve(instances_length);

        for (uint32_t i = 0; i < instances_length; ++i)
        {
            auto count = get_parent_count(i);
            first_parents.push_back(first_parent);
            first_parent += count;
            total_parent_count += count;
        }
    }

    // "parentIds is an array of integers whose length equals the sum of
    // the values in parentCounts. Parent ids are placed sequentially by
    // instance - instance 0's parent ids are followed by instance 1's
    // parent ids. Each value specifies the instance's parent as an index
    // into the classIds array."

    hrz::vector_data::PackedAttributeValuesReader parent_ids;
    {
        auto parent_ids_opt = _decode_u64_attribute_values(
            "parentIds", DummyAttributeId, hrz_proto::ATTRIBUTE_TRANSFORM_NONE,
            hrz::json::get_member_or_null(bth_json, "parentIds"), batch_table_bin_data,
            total_parent_count, blob_allocator, resource_owner);
        if (parent_ids_opt)
        {
            parent_ids = parent_ids_opt.value().get_reader();
        }
    }

    assert(first_parents.size() == instances_length);
    if (total_parent_count != parent_ids.values.size())
    {
        HRZ_LOG_ERROR("Invalid parentIds count");
        return false;
    }

    // "A feature's batchId is used to access its classId and parentCount.
    // Therefore, the values in the classIds and parentCounts arrays are
    // initially ordered by batchId and followed by non-feature instances."

    // "The parentCounts and parentIds arrays form an instance hierarchy. A
    // feature's properties include those defined by its own class and any
    // properties from ancestor instances."

    // "In some cases multiple ancestors may share the same property name.
    // This can occur if two ancestors are the same class or are different
    // classes with the same property names. For example, if every class
    // defined the property "id", then it would be an overloaded property.
    // In such cases it is up to the implementation to decide which value
    // to return."

    std::vector<hrz::vector_data::AttributeValuesBuilder> mutable_attributes;
    mutable_attributes.reserve(attributes.size());

    for (size_t i = 0; i < attributes.size(); ++i)
    {
        size_t size = attributes_to_load.contains(i) ? batch_length : 0;
        auto values =
            hrz::vector_data::AttributeValuesBuilder(size, blob_allocator, resource_owner);
        values.resize(size);
        mutable_attributes.push_back(std::move(values));
    }

    auto visit_class_fn = [&](uint64_t root_batch, uint64_t instance)
    {
        uint64_t index_in_class = instance_indices[instance];
        const auto& batch_class = batch_classes[class_ids.as_uint64(instance)];

        for (const auto& batch_attribute : batch_class.attributes)
        {
            mutable_attributes[batch_attribute.index_in_tileset].set_ref(
                root_batch, batch_attribute.reader.as_ref(index_in_class));
        }
    };

    std::function<void(uint64_t, uint64_t)> visit_fn = [&](uint64_t root_batch, uint64_t instance)
    {
        // We visit the potential parents first so that the properties
        // closest to the root batch overwrite the ones that may come from
        // parent instances.

        uint64_t parent_count = get_parent_count(instance);
        if (parent_count > 0)
        {
            uint64_t first_parent = first_parents[instance];
            for (uint64_t parent_offset = 0; parent_offset < parent_count; ++parent_offset)
            {
                uint64_t parent_instance = parent_ids.as_uint64(first_parent + parent_offset);
                if (parent_instance != instance)
                {
                    visit_fn(root_batch, parent_instance);
                }
            }
        }

        visit_class_fn(root_batch, instance);
    };

    for (uint64_t batch = 0; batch < batch_length; ++batch)
    {
        visit_fn(batch, batch);
    }

    if (class_id_attribute.has_value() || class_name_attribute.has_value())
    {
        for (uint64_t batch = 0; batch < batch_length; ++batch)
        {
            uint64_t class_id = class_ids.as_uint64(batch);

            if (class_id_attribute.has_value())
            {
                mutable_attributes[*class_id_attribute].set(batch, class_id);
            }

            if (class_name_attribute.has_value())
            {
                const auto& class_name = batch_classes[class_id].name;
                mutable_attributes[*class_name_attribute].set(batch, class_name);
            }
        }
    }

    for (size_t attribute_index : attributes_to_load)
    {
        const auto& attribute = attributes[attribute_index];
        attribute_values[attribute_index] = mutable_attributes[attribute_index].finalize(
            attribute.is_feature_id_attribute() ? attribute.attribute_id : DummyAttributeId);
    }

    return true;
}
} // namespace

hrz::JobResult finalize_attributes(
    const hrz::three_d_tiles::EncodedBatchTable& params,
    hrz::three_d_tiles::DecodedBatchTable& response,
    const JobContext& context)
{
    {
        bool has_all_id_attributes = true;
        hrz::InlinedVector<AttributeValues, 2> feature_id_attribute_values;

        for (unsigned int i = 0; i < params.attributes.size(); ++i)
        {
            const auto& attribute = params.attributes.at(i);
            if (attribute.is_feature_id_attribute())
            {
                if (response.attribute_values[i].has_value())
                {
                    feature_id_attribute_values.push_back(response.attribute_values.at(i).value());
                }
                else
                {
                    has_all_id_attributes = false;
                }
            }
        }

        if (has_all_id_attributes)
        {
            hrz::BlobVector<hrz::vector_data::FeatureIdHash> hashes(
                context.get_blob_allocator(), params.batch_length);
            hashes.register_blob_metadata("contents"_ss, "b3dm feature ID hashes"_ss);
            hashes.register_blob_owner(context.get_resource_owner());
            hashes.resize(params.batch_length);

            auto hashes_opt = hashes.to_blob_array();
            if (!hashes_opt.has_value())
            {
                HRZ_LOG_ERROR("Could not allocate feature ID hashes");
                return hrz::JobResult::FAILURE;
            }

            auto feature_ids = hrz::vector_data::FeatureIds::make(
                feature_id_attribute_values, std::move(hashes_opt.value()));
            if (feature_ids.has_value())
            {
                response.batches_to_feature_ids = std::move(feature_ids.value());
            }
            else
            {
                return hrz::JobResult::FAILURE;
            }
        }
    }

    for (unsigned int i = 0; i < params.attributes.size(); ++i)
    {
        const auto& attribute = params.attributes.at(i);

        if (!attribute.has_vector_data_layer_source()
            && !response.attribute_values.at(i).has_value())
        {
            HRZ_LOG_WARNING("Could not load values for attribute \"{}\"", attribute.name);

            auto null_values =
                hrz::BlobVector<AttributeValue>(context.get_blob_allocator(), params.batch_length);
            null_values.register_blob_owner(context.get_resource_owner());
            null_values.resize(params.batch_length);

            auto null_value = hrz::vector_data::attr_null<hrz::vector_data::PackedAttributeValue>();
            auto span_opt = null_values.data();
            if (span_opt)
            {
                auto span = span_opt.value();
                assert(span.size() == params.batch_length);
                for (size_t j = 0; j < params.batch_length; ++j)
                {
                    span[j] = null_value;
                }
            }

            auto null_values_opt = null_values.to_blob_array();
            if (!null_values_opt.has_value())
            {
                HRZ_LOG_ERROR(
                    "Could not allocate null attribute values for attribute \"{}\"",
                    attribute.name);
                return hrz::JobResult::FAILURE;
            }

            // Fill the attribute with null data to still allow styling.
            response.attribute_values[i] = {
                attribute.is_feature_id_attribute() ? attribute.attribute_id : DummyAttributeId,
                std::move(null_values_opt.value()),
                {}};
        }
    }

    return hrz::JobResult::SUCCESS;
}

hrz::JobResult run(
    const hrz::three_d_tiles::EncodedBatchTable& params,
    hrz::three_d_tiles::DecodedBatchTable& response,
    const JobContext& context)
{
    HRZ_SCOPED_SAMPLE("decode batch table job");

    response.attribute_values.resize(params.attributes.size(), std::nullopt);

    if (params.batch_length == 0)
    {
        if (!params.attributes.empty())
        {
            // Tiles without a batch table are stylable, but they have
            // no feature ids and attribute values.
            // In order to execute the styling script and have a computed
            // style for the tile, we pretend that there is one feature,
            // encompassing the whole tile.
            // Dummy attribute values are added to make the styling job
            // params valid, but they should obviously not be relied on.

            hrz::BlobVector<AttributeValue> dummy_value(context.get_blob_allocator(), 1);
            dummy_value.register_blob_metadata("contents"_ss, "b3dm dummy attribute value"_ss);
            dummy_value.register_blob_owner(context.get_resource_owner());
            dummy_value.push_back(
                hrz::vector_data::attr_null<hrz::vector_data::PackedAttributeValue>());

            auto dummy_value_opt = dummy_value.to_blob_array();
            if (!dummy_value_opt.has_value())
            {
                HRZ_LOG_ERROR("Could not allocate dummy value");
                return hrz::JobResult::FAILURE;
            }

            for (size_t i = 0; i < params.attributes.size(); ++i)
            {
                response.attribute_values.push_back(
                    {{DummyAttributeId, dummy_value_opt.value(), {}}});
            }
        }

        return hrz::JobResult::SUCCESS;
    }

    auto batch_table_json_data = params.json_data.get_data();
    if (batch_table_json_data.empty())
    {
        return finalize_attributes(params, response, context);
    }

    auto batch_table_bin_data = params.bin_data.get_data();

    rapidjson::Document document;
    document.Parse((const char*)batch_table_json_data.data(), batch_table_json_data.size());

    if (document.HasParseError())
    {
        HRZ_LOG_ERROR(
            "Could not parse batch table JSON: {}",
            rapidjson::GetParseError_En(document.GetParseError()));
        return hrz::JobResult::FAILURE;
    }

    if (!document.IsObject())
    {
        HRZ_LOG_ERROR("Invalid batch table JSON");
        return hrz::JobResult::FAILURE;
    }

    for (const auto& attribute_entry : document.GetObject())
    {
        std::string_view attribute_name = attribute_entry.name.GetString();
        if (attribute_name == "extensions") continue;

        for (unsigned int i = 0; i < params.attributes.size(); ++i)
        {
            const auto& attribute = params.attributes[i];

            if (attribute.has_batch_table_source() && attribute.name == attribute_name)
            {
                auto values = _decode_attribute_values(
                    attribute_name,
                    attribute.is_feature_id_attribute() ? attribute.attribute_id : DummyAttributeId,
                    attribute.transform, attribute_entry.value, batch_table_bin_data,
                    params.batch_length, context.get_blob_allocator(),
                    context.get_resource_owner());

                if (!values.has_value())
                {
                    HRZ_LOG_WARNING("Couldn't decode values for attribute {}", attribute_name);
                }

                response.attribute_values[i] = (std::move(values));
            }
        }
    }

    const auto& bth_json = hrz::json::get_nested_member_or_null(
        document, {"extensions", "3DTILES_batch_table_hierarchy"});

    if (!bth_json.IsNull() && bth_json.IsObject())
    {
        _decode_batch_table_hierarchy(
            params.batch_length, bth_json, batch_table_bin_data, params.attributes,
            response.attribute_values, context.get_blob_allocator(), context.get_resource_owner());
    }

    return finalize_attributes(params, response, context);
}
} // namespace hrz_jobs::decode_three_d_tiles_batch_table
