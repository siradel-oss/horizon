#include "hrz/common/model.h"
#include "hrz/common/profiling.h"
#include "hrz/core/jobs/jobs_declarations.h"
#include "hrz/fnd/flat_hash_map.h"
#include "hrz/fnd/log.h"
#include "hrz/fnd/maths.h"
#include "hrz/fnd/string_utils.h"

#include <draco/attributes/attribute_octahedron_transform.h>
#include <draco/attributes/attribute_quantization_transform.h>
#include <draco/compression/decode.h>

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <vector>

using hrz::model::Mesh;

namespace
{
// A constructor that is equivalent to this function is available in Draco 1.4.
draco::BoundingBox make_empty_bounding_box()
{
    return draco::BoundingBox(
        draco::Vector3f(
            std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max()),
        draco::Vector3f(
            -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
            -std::numeric_limits<float>::max()));
}

Mesh::Attribute::Type convert_vertex_type(draco::GeometryAttribute::Type type)
{
    switch (type)
    {
        case draco::GeometryAttribute::Type::POSITION:
            return Mesh::Attribute::Type::POSITION_ATTRIBUTE;
        case draco::GeometryAttribute::Type::NORMAL: return Mesh::Attribute::Type::NORMAL_ATTRIBUTE;
        case draco::GeometryAttribute::Type::COLOR: return Mesh::Attribute::Type::COLOR_ATTRIBUTE;
        case draco::GeometryAttribute::Type::TEX_COORD:
            return Mesh::Attribute::Type::TEXCOORD_ATTRIBUTE;
        case draco::GeometryAttribute::Type::GENERIC:
            return Mesh::Attribute::Type::GENERIC_ATTRIBUTE;
        default: assert(false && "Unhandled case"); return Mesh::Attribute::Type::GENERIC_ATTRIBUTE;
    }
}

Mesh::Attribute::DataType convert_vertex_data_type(draco::DataType data_type)
{
    switch (data_type)
    {
        case draco::DataType::DT_INT8: return Mesh::Attribute::DataType::INT8;
        case draco::DataType::DT_UINT8: return Mesh::Attribute::DataType::UINT8;
        case draco::DataType::DT_INT16: return Mesh::Attribute::DataType::INT16;
        case draco::DataType::DT_UINT16: return Mesh::Attribute::DataType::UINT16;
        case draco::DataType::DT_INT32:
        case draco::DataType::DT_INT64: return Mesh::Attribute::DataType::INT32;
        case draco::DataType::DT_UINT32:
        case draco::DataType::DT_UINT64: return Mesh::Attribute::DataType::UINT32;
        case draco::DataType::DT_FLOAT32:
        case draco::DataType::DT_FLOAT64: return Mesh::Attribute::DataType::FLOAT32;
        case draco::DataType::DT_BOOL: return Mesh::Attribute::DataType::INT8;
        default:
            HRZ_LOG_WARNING("Unsupported Draco vertex format: {}", fmt::underlying(data_type));
            return Mesh::Attribute::DataType::UINT8;
    }
}

/**
 * Return size in bytes.
 */
size_t get_vertex_data_type_size(Mesh::Attribute::DataType type)
{
    switch (type)
    {
        case Mesh::Attribute::DataType::INT8:
        case Mesh::Attribute::DataType::UINT8: return 1;
        case Mesh::Attribute::DataType::INT16:
        case Mesh::Attribute::DataType::UINT16: return 2;
        case Mesh::Attribute::DataType::INT32:
        case Mesh::Attribute::DataType::UINT32: return 4;
        case Mesh::Attribute::DataType::FLOAT16: return 2;
        case Mesh::Attribute::DataType::FLOAT32: return 4;
        default: assert(false && "Unhandled case"); return 0;
    }
}

Mesh::Attribute::DataType get_vertex_data_type_for_quantization_bits(uint32_t bits)
{
    if (bits <= 8)
    {
        return Mesh::Attribute::DataType::UINT8;
    }
    else if (bits <= 16)
    {
        return Mesh::Attribute::DataType::UINT16;
    }
    else
    {
        return Mesh::Attribute::DataType::UINT32;
    }
}

struct Attribute
{
    const draco::PointAttribute* draco;
    Mesh::Attribute::Compression compression;
    uint32_t quantization_bits = 0;
    std::array<float, 4> quantization_min_values = {0, 0, 0, 0};
    float quantization_range = 0;
    size_t size = 0;
    size_t offset = 0;
};

Mesh::Attribute::DataType get_vertex_data_type_for_attribute(const Attribute& attribute)
{
    if (attribute.compression == Mesh::Attribute::Compression::NONE)
    {
        return convert_vertex_data_type(attribute.draco->data_type());
    }
    else
    {
        return get_vertex_data_type_for_quantization_bits(attribute.quantization_bits);
    }
}

Attribute get_mesh_attribute(const draco::Mesh* mesh, int index)
{
    Attribute attribute;
    attribute.draco = mesh->attribute(index);

    draco::AttributeQuantizationTransform quantization;
    draco::AttributeOctahedronTransform octahedron;

    if (quantization.InitFromAttribute(*attribute.draco))
    {
        attribute.compression = Mesh::Attribute::Compression::QUANTIZED;
        attribute.quantization_bits = quantization.quantization_bits();
        for (unsigned int i = 0; i < quantization.min_values().size() && i < 4; ++i)
        {
            attribute.quantization_min_values[i] = quantization.min_value(i);
        }
        attribute.quantization_range = quantization.range();
    }
    else if (octahedron.InitFromAttribute(*attribute.draco))
    {
        attribute.compression = Mesh::Attribute::Compression::OCT_ENCODED;
        attribute.quantization_bits = octahedron.quantization_bits();
        attribute.quantization_range = 2;
    }
    else
    {
        attribute.compression = Mesh::Attribute::Compression::NONE;
    }

    attribute.size = get_vertex_data_type_size(get_vertex_data_type_for_attribute(attribute))
        * attribute.draco->num_components();

    return attribute;
}

size_t determine_attribute_offset(const Attribute& attribute, size_t current_offset)
{
    return hrz::align_up_po2<size_t>(current_offset, attribute.size >= 3 ? 4 : 2);
}

void copy_compressed_value(
    uint64_t values[4],
    uint8_t component_count,
    uint32_t quantization_bits,
    std::byte* dst)
{
    if (quantization_bits <= 8)
    {
        uint8_t u8_values[4];
        for (unsigned int i = 0; i < component_count; ++i)
        {
            u8_values[i] = (uint8_t)values[i];
        }
        std::memcpy(dst, u8_values, sizeof(uint8_t) * component_count);
    }
    else if (quantization_bits <= 16)
    {
        uint16_t u16_values[4];
        for (unsigned int i = 0; i < component_count; ++i)
        {
            u16_values[i] = (uint16_t)values[i];
        }
        std::memcpy(dst, u16_values, sizeof(uint16_t) * component_count);
    }
    else if (quantization_bits <= 32)
    {
        uint32_t u32_values[4];
        for (unsigned int i = 0; i < component_count; ++i)
        {
            u32_values[i] = (uint32_t)values[i];
        }
        std::memcpy(dst, u32_values, sizeof(uint32_t) * component_count);
    }
    else
    {
        // Only attribute values up to 32-bit wide are supported.
        // Values wider than that are right-shifted to fit in 32 bits.
        // This changes how the values must be scaled later on.

        uint32_t right_shift = quantization_bits - 32;
        uint32_t u32_values[4];
        for (unsigned int i = 0; i < component_count; ++i)
        {
            u32_values[i] = (uint32_t)(values[i] >> right_shift);
        }
        std::memcpy(dst, u32_values, sizeof(uint32_t) * component_count);
    }
}

void copy_attribute_value(
    const Attribute& attribute,
    uint8_t component_count,
    draco::AttributeValueIndex index,
    std::byte* dst,
    draco::BoundingBox& bbox)
{
    assert(component_count <= 4);

    bool update_bbox =
        attribute.draco->attribute_type() == draco::GeometryAttribute::Type::POSITION;

    if (attribute.compression == Mesh::Attribute::Compression::QUANTIZED)
    {
        uint64_t values[4];
        attribute.draco->ConvertValue<uint64_t>(index, values);
        int32_t bits = attribute.quantization_bits;
        copy_compressed_value(values, component_count, bits, dst);

        if (update_bbox)
        {
            // The method `PointCloud::ComputeBoundingBox()` exists, but
            // returns transformed values when `SetSkipAttributeTransform()`
            // has been called for the attribute.
            // So we have to compute the positions here instead, in order
            // to be able to return the actual bounding box from the job.

            const auto& mins = attribute.quantization_min_values;
            float range = attribute.quantization_range;
            float scale = range / (float)((1 << bits) - 1);

            draco::Vector3f float_values;
            for (unsigned int i = 0; i < 3 && i < component_count; ++i)
            {
                float_values[i] = (float)values[i] * scale + mins[i];
            }
            bbox.Update(float_values);
        }

        return;
    }
    else if (attribute.compression == Mesh::Attribute::Compression::OCT_ENCODED)
    {
        uint64_t values[4];
        attribute.draco->ConvertValue<uint64_t>(index, values);
        int32_t bits = attribute.quantization_bits;
        copy_compressed_value(values, component_count, bits, dst);
        return;
    }

    attribute.draco->GetValue(index, (void*)dst);

    if (update_bbox)
    {
        draco::Vector3f values;
        attribute.draco->ConvertValue<float>(index, 3, values.data());
        bbox.Update(values);
    }
}

template<typename TOut>
void copy_convert_attribute_value(
    const Attribute& attribute,
    uint8_t component_count,
    draco::AttributeValueIndex index,
    std::byte* dst,
    draco::BoundingBox& bbox)
{
    assert(component_count > 0 || component_count <= 4);

    TOut data[4];
    attribute.draco->ConvertValue<TOut>(index, data);
    std::memcpy((void*)dst, (void*)&data, sizeof(TOut) * component_count);

    if (attribute.draco->attribute_type() == draco::GeometryAttribute::Type::POSITION)
    {
        draco::Vector3f values;
        attribute.draco->ConvertValue<float>(index, 3, values.data());
        bbox.Update(values);
    }
}

/**
 * The returned bounding box is only valid for the POSITION vertex attribute.
 */
draco::BoundingBox add_attribute_to_response(
    const Attribute& attribute,
    size_t vertex_count,
    std::byte* vertex_buffer,
    size_t stride)
{
    HRZ_SCOPED_SAMPLE("add attribute to response");

    auto data_type = attribute.draco->data_type();
    auto component_count = attribute.draco->num_components();
    auto copy_func = copy_attribute_value;

    draco::BoundingBox bbox = make_empty_bounding_box();

    // 64-bit attributes are not supported in the rest of the engine,
    // so they are converted to 32 bit here.
    if (attribute.compression == Mesh::Attribute::Compression::NONE)
    {
        switch (data_type)
        {
            case draco::DataType::DT_INT64:
                stride = sizeof(int32_t);
                copy_func = copy_convert_attribute_value<int32_t>;
            case draco::DataType::DT_UINT64:
                stride = sizeof(uint32_t);
                copy_func = copy_convert_attribute_value<uint32_t>;
            case draco::DataType::DT_FLOAT64:
                stride = sizeof(float_t);
                copy_func = copy_convert_attribute_value<float_t>;
                break;
            default: break;
        }
    }

    for (unsigned int i = 0; i < vertex_count; ++i)
    {
        auto attribute_value_index = attribute.draco->mapped_index(draco::PointIndex(i));
        std::byte* dst = vertex_buffer + stride * i;
        copy_func(attribute, component_count, attribute_value_index, dst, bbox);
    }

    return bbox;
}

void add_compression_params_to_response(
    const Attribute& mesh_attribute,
    Mesh::Attribute& response_attribute)
{
    response_attribute.compression = mesh_attribute.compression;
    response_attribute.quantization_bits = std::min(mesh_attribute.quantization_bits, (uint32_t)32);
    for (int i = 0; i < mesh_attribute.draco->num_components() && i < 4; ++i)
    {
        response_attribute.quantization_mins.push_back(mesh_attribute.quantization_min_values[i]);
    }
    response_attribute.quantization_range = mesh_attribute.quantization_range;
}

template<typename T>
std::optional<hrz::blobs::BlobHandle> add_indices_to_response(
    const draco::Mesh* mesh,
    hrz::BlobAllocator* blob_allocator,
    const hrz::monitoring::ResourceOwner& resource_owner)
{
    HRZ_SCOPED_SAMPLE("add indices to response");

    auto blob = hrz::blobs::allocate_blob_sync(blob_allocator, mesh->num_faces() * 3 * sizeof(T));
    if (!blob.has_value()) return std::nullopt;

    hrz::blobs::register_metadata(
        blob_allocator, blob.value(), "type"_ss, "Draco mesh index buffer"_ss);
    hrz::blobs::register_owner(blob_allocator, blob.value(), resource_owner);

    {
        auto index_buffer = blob->get_mutable_data();

        for (unsigned int i = 0; i < mesh->num_faces(); ++i)
        {
            const auto& face = mesh->face(draco::FaceIndex(i));
            for (unsigned int j = 0; j < 3; ++j)
            {
                T value = face[j].value();
                std::memcpy(index_buffer.data() + (i * 3 + j) * sizeof(T), &value, sizeof(T));
            }
        }
    }

    return blob;
}
} // namespace

namespace hrz_jobs::decompress_draco_mesh
{
hrz::JobResult run(
    const hrz::blobs::BlobHandle& compressed_mesh,
    Mesh& response,
    const JobContext& context)
{
    HRZ_SCOPED_SAMPLE("decompress draco mesh");

    auto compressed_mesh_data = compressed_mesh.get_data();
    draco::DecoderBuffer buffer;
    buffer.Init((const char*)compressed_mesh_data.data(), compressed_mesh_data.size());

    auto geometry_type = draco::Decoder::GetEncodedGeometryType(&buffer);

    if (!geometry_type.ok())
    {
        HRZ_LOG_ERROR(
            "Error while decoding compressed mesh: {}", geometry_type.status().error_msg_string());
        return hrz::JobResult::FAILURE;
    }

    if (geometry_type.value() != draco::TRIANGULAR_MESH)
    {
        HRZ_LOG_ERROR("Error: Only triangular meshes are supported");
        return hrz::JobResult::FAILURE;
    }

    draco::Decoder decoder;
    decoder.SetSkipAttributeTransform(draco::GeometryAttribute::POSITION);
    decoder.SetSkipAttributeTransform(draco::GeometryAttribute::NORMAL);
    decoder.SetSkipAttributeTransform(draco::GeometryAttribute::TEX_COORD);
    // Colours are usually already in RGBA8 format, which is optimal.
    auto mesh_or_error = decoder.DecodeMeshFromBuffer(&buffer);

    if (!mesh_or_error.ok())
    {
        HRZ_LOG_ERROR(
            "Error while decompressing mesh: {}", mesh_or_error.status().error_msg_string());
        return hrz::JobResult::FAILURE;
    }

    auto mesh = mesh_or_error.value().get();
    const uint32_t vertex_count = mesh->num_points();

    std::vector<Attribute> attributes;
    attributes.reserve((size_t)mesh->num_attributes());

    for (int i = 0; i < mesh->num_attributes(); ++i)
    {
        attributes.push_back(get_mesh_attribute(mesh, i));
    }

    std::ranges::sort(
        attributes, [](const Attribute& a, const Attribute& b) { return a.size > b.size; });

    size_t vertex_stride = 0;
    {
        // Align attributes of 4 bytes of more on 4 bytes.
        // Align smaller attributes on 2 bytes.

        size_t current_offset = 0;
        std::vector<bool> placed_attributes(attributes.size(), false);
        for (unsigned int i = 0; i < attributes.size(); ++i)
        {
            if (placed_attributes[i]) continue;
            auto& attribute_i = attributes.at(i);
            attribute_i.offset = determine_attribute_offset(attribute_i, current_offset);
            placed_attributes[i] = true;
            current_offset = attribute_i.offset + attribute_i.size;

            size_t to_next_4 = hrz::align_up_po2<size_t>(current_offset, 4) - current_offset;

            // Try to squeeze a small attribute in the leftover space.
            for (unsigned int j = 0; j < attributes.size(); ++j)
            {
                if (placed_attributes[j]) continue;
                auto& attribute_j = attributes.at(j);
                if (attribute_j.size <= to_next_4)
                {
                    attribute_j.offset = determine_attribute_offset(attribute_j, current_offset);
                    placed_attributes[j] = true;
                    current_offset = attribute_j.offset + attribute_j.size;
                    break;
                }
            }
        }
        vertex_stride = hrz::align_up_po2<size_t>(current_offset, 4);
    }

    size_t total_buffer_size = vertex_stride * vertex_count;

    auto vertex_data_blob =
        hrz::blobs::allocate_blob_sync(context.get_blob_allocator(), total_buffer_size);
    if (!vertex_data_blob.has_value())
    {
        return hrz::JobResult::FAILURE;
    }

    hrz::blobs::register_metadata(
        context.get_blob_allocator(), vertex_data_blob.value(), "type", "Draco mesh vertex buffer");
    hrz::blobs::register_owner(
        context.get_blob_allocator(), vertex_data_blob.value(), context.get_resource_owner());

    draco::BoundingBox bounding_box = make_empty_bounding_box();

    {
        auto vertex_buffer = vertex_data_blob->get_mutable_data();

        for (const auto& attribute : attributes)
        {
            std::byte* dst = vertex_buffer.data() + attribute.offset;
            auto attribute_bbox =
                add_attribute_to_response(attribute, vertex_count, dst, vertex_stride);

            if (attribute.draco->attribute_type() == draco::GeometryAttribute::Type::POSITION)
            {
                bounding_box = attribute_bbox;
            }
        }
    }

    response.vertex_data = std::move(vertex_data_blob.value());

    std::optional<hrz::blobs::BlobHandle> indices_blob = std::nullopt;
    uint32_t index_count = mesh->num_faces() * 3;
    if (index_count <= std::numeric_limits<uint8_t>::max())
    {
        indices_blob = add_indices_to_response<uint8_t>(
            mesh, context.get_blob_allocator(), context.get_resource_owner());
        response.index_type = Mesh::IndexType::UBYTE;
    }
    else if (index_count <= std::numeric_limits<uint16_t>::max())
    {
        indices_blob = add_indices_to_response<uint16_t>(
            mesh, context.get_blob_allocator(), context.get_resource_owner());
        response.index_type = Mesh::IndexType::USHORT;
    }
    else if (index_count <= std::numeric_limits<uint32_t>::max())
    {
        indices_blob = add_indices_to_response<uint32_t>(
            mesh, context.get_blob_allocator(), context.get_resource_owner());
        response.index_type = Mesh::IndexType::UINT;
    }
    else
    {
        HRZ_LOG_ERROR("Too many indices: {}", index_count);
        return hrz::JobResult::FAILURE;
    }
    if (indices_blob.has_value())
    {
        response.index_data = std::move(indices_blob.value());
        response.index_count = index_count;
    }
    else
    {
        return hrz::JobResult::FAILURE;
    }

    for (const auto& mesh_attribute : attributes)
    {
        Mesh::Attribute response_attribute;

        response_attribute.id = mesh_attribute.draco->unique_id();
        response_attribute.type = convert_vertex_type(mesh_attribute.draco->attribute_type());
        response_attribute.normalized = mesh_attribute.draco->normalized();
        response_attribute.data_type = get_vertex_data_type_for_attribute(mesh_attribute);
        response_attribute.component_count = mesh_attribute.draco->num_components();
        response_attribute.offset = mesh_attribute.offset;
        response_attribute.stride = vertex_stride;
        add_compression_params_to_response(mesh_attribute, response_attribute);

        assert(
            response_attribute.offset + response_attribute.stride * (vertex_count - 1)
                + mesh_attribute.size
            <= total_buffer_size);

        response.attributes.push_back(response_attribute);
    }

    response.bounding_box.min.x = bounding_box.GetMinPoint()[0];
    response.bounding_box.min.y = bounding_box.GetMinPoint()[1];
    response.bounding_box.min.z = bounding_box.GetMinPoint()[2];
    response.bounding_box.max.x = bounding_box.GetMaxPoint()[0];
    response.bounding_box.max.y = bounding_box.GetMaxPoint()[1];
    response.bounding_box.max.z = bounding_box.GetMaxPoint()[2];

    return hrz::JobResult::SUCCESS;
}
} // namespace hrz_jobs::decompress_draco_mesh
