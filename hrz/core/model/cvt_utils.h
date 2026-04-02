#pragma once

#include "hrz/core/jobs/decompress_draco_mesh.h"
#include "hrz/core/model/ubo_defs.h"

#include <mycelium/backend.h>

namespace hrz::model
{

[[maybe_unused]]
static my::VertexFormat _convert_vertex_format(
    hrz_jobs::DecompressedDracoMesh::Attribute::DataType data_type,
    unsigned int component_count)
{
    switch (data_type)
    {
        case hrz_jobs::DecompressedDracoMesh::Attribute::DataType::INT8:
        {
            switch (component_count)
            {
                case 1: return my::VertexFormat::Int8;
                case 2: return my::VertexFormat::Int8_2;
                case 3: return my::VertexFormat::Int8_3;
                case 4: return my::VertexFormat::Int8_4;
                default: break;
            }
            break;
        }
        case hrz_jobs::DecompressedDracoMesh::Attribute::DataType::UINT8:
        {
            switch (component_count)
            {
                case 1: return my::VertexFormat::UInt8;
                case 2: return my::VertexFormat::UInt8_2;
                case 3: return my::VertexFormat::UInt8_3;
                case 4: return my::VertexFormat::UInt8_4;
                default: break;
            }
            break;
        }
        case hrz_jobs::DecompressedDracoMesh::Attribute::DataType::INT16:
        {
            switch (component_count)
            {
                case 1: return my::VertexFormat::Int16;
                case 2: return my::VertexFormat::Int16_2;
                case 3: return my::VertexFormat::Int16_3;
                case 4: return my::VertexFormat::Int16_4;
                default: break;
            }
            break;
        }
        case hrz_jobs::DecompressedDracoMesh::Attribute::DataType::UINT16:
        {
            switch (component_count)
            {
                case 1: return my::VertexFormat::UInt16;
                case 2: return my::VertexFormat::UInt16_2;
                case 3: return my::VertexFormat::UInt16_3;
                case 4: return my::VertexFormat::UInt16_4;
                default: break;
            }
            break;
        }
        case hrz_jobs::DecompressedDracoMesh::Attribute::DataType::INT32:
        {
            switch (component_count)
            {
                case 1: return my::VertexFormat::Int32;
                case 2: return my::VertexFormat::Int32_2;
                case 3: return my::VertexFormat::Int32_3;
                case 4: return my::VertexFormat::Int32_4;
                default: break;
            }
            break;
        }
        case hrz_jobs::DecompressedDracoMesh::Attribute::DataType::UINT32:
        {
            switch (component_count)
            {
                case 1: return my::VertexFormat::UInt32;
                case 2: return my::VertexFormat::UInt32_2;
                case 3: return my::VertexFormat::UInt32_3;
                case 4: return my::VertexFormat::UInt32_4;
                default: break;
            }
            break;
        }
        case hrz_jobs::DecompressedDracoMesh::Attribute::DataType::FLOAT16:
        {
            switch (component_count)
            {
                case 1: return my::VertexFormat::Float16;
                case 2: return my::VertexFormat::Float16_2;
                case 3: return my::VertexFormat::Float16_3;
                case 4: return my::VertexFormat::Float16_4;
                default: break;
            }
            break;
        }
        case hrz_jobs::DecompressedDracoMesh::Attribute::DataType::FLOAT32:
        {
            switch (component_count)
            {
                case 1: return my::VertexFormat::Float32;
                case 2: return my::VertexFormat::Float32_2;
                case 3: return my::VertexFormat::Float32_3;
                case 4: return my::VertexFormat::Float32_4;
                default: break;
            }
            break;
        }
        default: break;
    }

    assert(false && "Unhandled case");
    return my::VertexFormat::UInt8;
}

inline my::IndexType _convert_index_type(hrz_jobs::DecompressedDracoMesh::IndexType index_type)
{
    switch (index_type)
    {
        using enum hrz_jobs::DecompressedDracoMesh::IndexType;
        case UBYTE: return my::IndexType::UByte;
        case USHORT: return my::IndexType::UShort;
        case UINT: return my::IndexType::UInt;
        default: assert(false && "Unhandled case"); return my::IndexType::UShort;
    }
}

inline DracoCompressionType _convert_compression_type(
    hrz_jobs::DecompressedDracoMesh::Attribute::Compression compression)
{
    switch (compression)
    {
        using enum hrz_jobs::DecompressedDracoMesh::Attribute::Compression;
        case NONE: return DracoCompressionType::None;
        case QUANTIZED: return DracoCompressionType::Quantized;
        case OCT_ENCODED: return DracoCompressionType::OctEncoded;
        default: assert(false && "Unhanded case"); return DracoCompressionType::None;
    }
}

[[maybe_unused]]
static VertexCompressionParamsUniformData _to_compression_uniform_data(
    const hrz_jobs::DecompressedDracoMesh::Attribute& attr)
{
    using Type = DracoCompressionType;

    const Type type = _convert_compression_type(attr.compression);

    VertexCompressionParamsUniformData uniform_data;
    uniform_data.type = type;

    for (size_t i = 0; i < attr.quantization_mins.size() && i < 3; ++i)
    {
        uniform_data.quantization_mins.m[i] = attr.quantization_mins.at(i);
    }

    if (type == Type::None || attr.quantization_bits == 0)
    {
        uniform_data.quantization_scale = lm::vec3(1);
    }
    else if (type == Type::Quantized)
    {
        uniform_data.quantization_scale =
            lm::vec3(attr.quantization_range / (float)((1 << attr.quantization_bits) - 1));
    }
    else if (type == Type::OctEncoded)
    {
        // Once decoded, the values always go from -1 to 1, so the range
        // is always 2. Shifting values from [0,2] to [-1,1] is done in
        // the vertex shader.
        // For oct-encoded values, the maximum value is 1 less than the
        // maximum encodable value, hence `-2`. This is likely to allow
        // encoding 0.
        // See normal_compression_utils.h in the Draco sources.
        uniform_data.quantization_scale =
            lm::vec3(2.0F / (float)((1 << attr.quantization_bits) - 2));
    }
    else
    {
        assert(false && "Unhandled case");
    }
    return uniform_data;
}

} // namespace hrz::model
