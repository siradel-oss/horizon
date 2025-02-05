#pragma once

#include "model/hrz_core_model_ubo_defs.h"

#include <hrz_common_model.h>
#include <hrz_fnd_log.h>
#include <hrz_jobs_protocol.h>

#include <mycelium_backend.h>

namespace hrz::model
{
static my::VertexFormat _convert_vertex_format(
    Mesh::Attribute::DataType data_type,
    unsigned int component_count)
{
    switch (data_type)
    {
        case Mesh::Attribute::DataType::INT8:
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
        case Mesh::Attribute::DataType::UINT8:
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
        case Mesh::Attribute::DataType::INT16:
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
        case Mesh::Attribute::DataType::UINT16:
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
        case Mesh::Attribute::DataType::INT32:
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
        case Mesh::Attribute::DataType::UINT32:
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
        case Mesh::Attribute::DataType::FLOAT16:
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
        case Mesh::Attribute::DataType::FLOAT32:
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

static my::IndexType _convert_index_type(Mesh::IndexType index_type)
{
    switch (index_type)
    {
        case Mesh::IndexType::UBYTE: return my::IndexType::UByte;
        case Mesh::IndexType::USHORT: return my::IndexType::UShort;
        case Mesh::IndexType::UINT: return my::IndexType::UInt;
        default: assert(false && "Unhandled case"); return my::IndexType::UShort;
    }
}

static DracoCompressionType _convert_compression_type(Mesh::Attribute::Compression compression)
{
    switch (compression)
    {
        case Mesh::Attribute::Compression::NONE: return DracoCompressionType::None;
        case Mesh::Attribute::Compression::QUANTIZED: return DracoCompressionType::Quantized;
        case Mesh::Attribute::Compression::OCT_ENCODED: return DracoCompressionType::OctEncoded;
        default: assert(false && "Unhanded case"); return DracoCompressionType::None;
    }
}

static VertexCompressionParamsUniformData _to_compression_uniform_data(const Mesh::Attribute& attr)
{
    using Type = DracoCompressionType;

    Type type = _convert_compression_type(attr.compression);

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
            lm::vec3(2.0f / (float)((1 << attr.quantization_bits) - 2));
    }
    else
    {
        assert(false && "Unhandled case");
    }
    return uniform_data;
}

static my::IndexType _get_index_type(my::VertexFormat format)
{
    switch (format)
    {
        case my::VertexFormat::UInt8: return my::IndexType::UByte;
        case my::VertexFormat::UInt16: return my::IndexType::UShort;
        case my::VertexFormat::UInt32: return my::IndexType::UInt;
        default:
            HRZ_LOG_WARNING("Index type {} not supported", (int)format);
            return my::IndexType::UShort;
    }
}

} // namespace hrz::model
