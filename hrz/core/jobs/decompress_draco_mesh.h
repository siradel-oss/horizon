#pragma once

#include "hrz/common/blob_allocator.h"

#include <lin_maths.h>

#include <vector>

namespace hrz_jobs
{

struct DecompressedDracoMesh
{
    enum class IndexType
    {
        UBYTE,
        USHORT,
        UINT
    };

    struct Attribute
    {
        enum class Type
        {
            POSITION_ATTRIBUTE,
            NORMAL_ATTRIBUTE,
            COLOR_ATTRIBUTE,
            TEXCOORD_ATTRIBUTE,
            GENERIC_ATTRIBUTE
        };

        enum class DataType
        {
            INT8,
            UINT8,
            INT16,
            UINT16,
            INT32,
            UINT32,
            FLOAT16,
            FLOAT32
        };

        enum class Compression
        {
            NONE,
            QUANTIZED,
            OCT_ENCODED
        };

        uint32_t id;
        Type type;
        DataType data_type;
        bool normalized;
        uint32_t component_count;
        uint32_t offset;
        uint32_t stride;
        Compression compression;
        uint32_t quantization_bits;
        std::vector<float> quantization_mins;
        float quantization_range;
    };

    hrz::blobs::BlobHandle vertex_data;
    hrz::blobs::BlobHandle index_data;
    uint32_t index_count;
    IndexType index_type;
    std::vector<Attribute> attributes;
    lm::dbbox3 bounding_box;
};

} // namespace hrz_jobs
