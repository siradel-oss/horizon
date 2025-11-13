#pragma once

#include "mycelium/backend.h"

#include <assert.h>

namespace my
{

constexpr VertexFormat to_normalized(VertexFormat format)
{
    switch (format)
    {
        case VertexFormat::Int8: return VertexFormat::Int8Norm;
        case VertexFormat::Int8_2: return VertexFormat::Int8Norm_2;
        case VertexFormat::Int8_3: return VertexFormat::Int8Norm_3;
        case VertexFormat::Int8_4: return VertexFormat::Int8Norm_4;
        case VertexFormat::UInt8: return VertexFormat::UInt8Norm;
        case VertexFormat::UInt8_2: return VertexFormat::UInt8Norm_2;
        case VertexFormat::UInt8_3: return VertexFormat::UInt8Norm_3;
        case VertexFormat::UInt8_4: return VertexFormat::UInt8Norm_4;
        case VertexFormat::Int16: return VertexFormat::Int16Norm;
        case VertexFormat::Int16_2: return VertexFormat::Int16Norm_2;
        case VertexFormat::Int16_3: return VertexFormat::Int16Norm_3;
        case VertexFormat::Int16_4: return VertexFormat::Int16Norm_4;
        case VertexFormat::UInt16: return VertexFormat::UInt16Norm;
        case VertexFormat::UInt16_2: return VertexFormat::UInt16Norm_2;
        case VertexFormat::UInt16_3: return VertexFormat::UInt16Norm_3;
        case VertexFormat::UInt16_4: return VertexFormat::UInt16Norm_4;
        case VertexFormat::Int32: return VertexFormat::Int32Norm;
        case VertexFormat::Int32_2: return VertexFormat::Int32Norm_2;
        case VertexFormat::Int32_3: return VertexFormat::Int32Norm_3;
        case VertexFormat::Int32_4: return VertexFormat::Int32Norm_4;
        case VertexFormat::UInt32: return VertexFormat::UInt32Norm;
        case VertexFormat::UInt32_2: return VertexFormat::UInt32Norm_2;
        case VertexFormat::UInt32_3: return VertexFormat::UInt32Norm_3;
        case VertexFormat::UInt32_4: return VertexFormat::UInt32Norm_4;
        default: return format;
    }
}

constexpr size_t vertex_size(VertexFormat format)
{
    switch (format)
    {
        case VertexFormat::Int8:
        case VertexFormat::Int8Norm:
        case VertexFormat::UInt8:
        case VertexFormat::UInt8Norm: return 1;
        case VertexFormat::Int16:
        case VertexFormat::Int16Norm:
        case VertexFormat::UInt16:
        case VertexFormat::UInt16Norm:
        case VertexFormat::Float16:
        case VertexFormat::Int8_2:
        case VertexFormat::Int8Norm_2:
        case VertexFormat::UInt8_2:
        case VertexFormat::UInt8Norm_2: return 2;
        case VertexFormat::Int8_3:
        case VertexFormat::Int8Norm_3:
        case VertexFormat::UInt8_3:
        case VertexFormat::UInt8Norm_3: return 3;
        case VertexFormat::Int32:
        case VertexFormat::Int32Norm:
        case VertexFormat::UInt32:
        case VertexFormat::UInt32Norm:
        case VertexFormat::Float32:
        case VertexFormat::Int16_2:
        case VertexFormat::Int16Norm_2:
        case VertexFormat::UInt16_2:
        case VertexFormat::UInt16Norm_2:
        case VertexFormat::Float16_2:
        case VertexFormat::Int8_4:
        case VertexFormat::Int8Norm_4:
        case VertexFormat::UInt8_4:
        case VertexFormat::UInt8Norm_4: return 4;
        case VertexFormat::Int16_3:
        case VertexFormat::Int16Norm_3:
        case VertexFormat::UInt16_3:
        case VertexFormat::UInt16Norm_3:
        case VertexFormat::Float16_3: return 6;
        case VertexFormat::Int32_2:
        case VertexFormat::Int32Norm_2:
        case VertexFormat::UInt32_2:
        case VertexFormat::UInt32Norm_2:
        case VertexFormat::Float32_2:
        case VertexFormat::Int16_4:
        case VertexFormat::Int16Norm_4:
        case VertexFormat::UInt16_4:
        case VertexFormat::UInt16Norm_4:
        case VertexFormat::Float16_4: return 8;
        case VertexFormat::Int32_3:
        case VertexFormat::Int32Norm_3:
        case VertexFormat::UInt32_3:
        case VertexFormat::UInt32Norm_3:
        case VertexFormat::Float32_3: return 12;
        case VertexFormat::Int32_4:
        case VertexFormat::Int32Norm_4:
        case VertexFormat::UInt32_4:
        case VertexFormat::UInt32Norm_4:
        case VertexFormat::Float32_4: return 16;
        default:
        {
            assert(!"Unknown vertex format");
            return 0;
        }
    }
}

constexpr size_t index_size(IndexType type)
{
    switch (type)
    {
        case IndexType::UByte: return 1;
        case IndexType::UShort: return 2;
        case IndexType::UInt: return 4;
        default: assert(!"Unknown index type"); return 0;
    }
}

constexpr bool is_format_compressed(TextureFormat format)
{
    switch (format)
    {
        case TextureFormat::RGB_BC1:
        case TextureFormat::RGBA_BC1:
        case TextureFormat::RGBA_BC2:
        case TextureFormat::RGBA_BC3:
        case TextureFormat::RGBA_BC7:
        case TextureFormat::RGB_ETC1:
        case TextureFormat::RGB_ETC2:
        case TextureFormat::RGBA_ETC2_EAC:
        case TextureFormat::RGB_PVRTC1_4BPP:
        case TextureFormat::RGBA_PVRTC1_4BPP:
        case TextureFormat::RGBA_PVRTC2_4BPP:
        case TextureFormat::RGBA_ASTC_4x4:
        case TextureFormat::SRGB_BC1:
        case TextureFormat::SRGBA_BC1:
        case TextureFormat::SRGBA_BC2:
        case TextureFormat::SRGBA_BC3:
        case TextureFormat::SRGBA_BC7:
        case TextureFormat::SRGB_ETC1:
        case TextureFormat::SRGB_ETC2:
        case TextureFormat::SRGBA_ETC2_EAC:
        case TextureFormat::SRGB_PVRTC1_4BPP:
        case TextureFormat::SRGBA_PVRTC1_4BPP:
        case TextureFormat::SRGBA_PVRTC2_4BPP:
        case TextureFormat::SRGBA_ASTC_4x4: return true;
        default: return false;
    }
}

// Don't use this function to compute the byte size of textures, because
// of compressed formats. Instead, create a TextureLayout and use its
// method for computing the size.
constexpr size_t format_external_pixel_byte_size(TextureFormat format)
{
    switch (format)
    {
        case TextureFormat::RGB_BC1:           // Actually 4 bits per pixel
        case TextureFormat::RGB_ETC1:          // Actually 4 bits per pixel
        case TextureFormat::RGB_ETC2:          // Actually 4 bits per pixel
        case TextureFormat::RGB_PVRTC1_4BPP:   // Actually 4 bits per pixel
        case TextureFormat::RGBA_PVRTC1_4BPP:  // Actually 4 bits per pixel
        case TextureFormat::RGBA_PVRTC2_4BPP:  // Actually 4 bits per pixel
        case TextureFormat::SRGB_BC1:          // Actually 4 bits per pixel
        case TextureFormat::SRGB_ETC1:         // Actually 4 bits per pixel
        case TextureFormat::SRGB_ETC2:         // Actually 4 bits per pixel
        case TextureFormat::SRGB_PVRTC1_4BPP:  // Actually 4 bits per pixel
        case TextureFormat::SRGBA_PVRTC1_4BPP: // Actually 4 bits per pixel
        case TextureFormat::SRGBA_PVRTC2_4BPP: // Actually 4 bits per pixel
            return 1;
        case TextureFormat::R8:
        case TextureFormat::R8I:
        case TextureFormat::R8UI:
        case TextureFormat::RGBA_BC1:
        case TextureFormat::RGBA_BC2:
        case TextureFormat::RGBA_BC3:
        case TextureFormat::RGBA_BC7:
        case TextureFormat::RGBA_ETC2_EAC:
        case TextureFormat::RGBA_ASTC_4x4:
        case TextureFormat::SRGBA_BC1:
        case TextureFormat::SRGBA_BC2:
        case TextureFormat::SRGBA_BC3:
        case TextureFormat::SRGBA_BC7:
        case TextureFormat::SRGBA_ETC2_EAC:
        case TextureFormat::SRGBA_ASTC_4x4: return 1;
        case TextureFormat::R16F:
        case TextureFormat::R16I:
        case TextureFormat::R16UI:
        case TextureFormat::RG8:
        case TextureFormat::RG8I:
        case TextureFormat::RG8UI:
        case TextureFormat::Depth16: return 2;
        case TextureFormat::RGB8:
        case TextureFormat::RGB8I:
        case TextureFormat::RGB8UI: return 3;
        case TextureFormat::R32F:
        case TextureFormat::R32I:
        case TextureFormat::R32UI:
        case TextureFormat::RG16F:
        case TextureFormat::RG16I:
        case TextureFormat::RG16UI:
        case TextureFormat::RGBA8:
        case TextureFormat::RGBA8I:
        case TextureFormat::RGBA8UI:
        case TextureFormat::SRGBA8:
        case TextureFormat::Depth24:
        case TextureFormat::Depth32F:
        case TextureFormat::Depth24Stencil8: return 4;
        case TextureFormat::RGB16I:
        case TextureFormat::RGB16UI:
        case TextureFormat::RGB16F: return 6;
        case TextureFormat::RG32F:
        case TextureFormat::RG32I:
        case TextureFormat::RG32UI:
        case TextureFormat::RGBA16F:
        case TextureFormat::RGBA16I:
        case TextureFormat::RGBA16UI:
        case TextureFormat::Depth32FStencil8: return 8;
        case TextureFormat::RGB32F:
        case TextureFormat::RGB32I:
        case TextureFormat::RGB32UI: return 12;
        case TextureFormat::RGBA32F:
        case TextureFormat::RGBA32I:
        case TextureFormat::RGBA32UI: return 16;
        default:
        {
            assert(!"Unknown texture format");
            return 0;
        }
    }
}

// The values returned by this function are educated guesses.
// Do not use with compressed texture formats.
constexpr size_t format_internal_pixel_byte_size(TextureFormat format)
{
    switch (format)
    {
        case TextureFormat::RGB_BC1:           // Actually 4 bits per pixel
        case TextureFormat::RGB_ETC1:          // Actually 4 bits per pixel
        case TextureFormat::RGB_ETC2:          // Actually 4 bits per pixel
        case TextureFormat::RGB_PVRTC1_4BPP:   // Actually 4 bits per pixel
        case TextureFormat::RGBA_PVRTC1_4BPP:  // Actually 4 bits per pixel
        case TextureFormat::RGBA_PVRTC2_4BPP:  // Actually 4 bits per pixel
        case TextureFormat::SRGB_BC1:          // Actually 4 bits per pixel
        case TextureFormat::SRGB_ETC1:         // Actually 4 bits per pixel
        case TextureFormat::SRGB_ETC2:         // Actually 4 bits per pixel
        case TextureFormat::SRGB_PVRTC1_4BPP:  // Actually 4 bits per pixel
        case TextureFormat::SRGBA_PVRTC1_4BPP: // Actually 4 bits per pixel
        case TextureFormat::SRGBA_PVRTC2_4BPP: // Actually 4 bits per pixel
            return 1;
        case TextureFormat::R8:
        case TextureFormat::R8I:
        case TextureFormat::R8UI:
        case TextureFormat::RGBA_BC1:
        case TextureFormat::RGBA_BC2:
        case TextureFormat::RGBA_BC3:
        case TextureFormat::RGBA_BC7:
        case TextureFormat::RGBA_ETC2_EAC:
        case TextureFormat::RGBA_ASTC_4x4:
        case TextureFormat::SRGBA_BC1:
        case TextureFormat::SRGBA_BC2:
        case TextureFormat::SRGBA_BC3:
        case TextureFormat::SRGBA_BC7:
        case TextureFormat::SRGBA_ETC2_EAC:
        case TextureFormat::SRGBA_ASTC_4x4: return 1;
        case TextureFormat::R16F:
        case TextureFormat::R16I:
        case TextureFormat::R16UI:
        case TextureFormat::RG8:
        case TextureFormat::RG8I:
        case TextureFormat::RG8UI:
        case TextureFormat::Depth16: return 2;
        case TextureFormat::RGB8:
        case TextureFormat::RGB8I:
        case TextureFormat::RGB8UI:
        case TextureFormat::R32F:
        case TextureFormat::R32I:
        case TextureFormat::R32UI:
        case TextureFormat::RG16F:
        case TextureFormat::RG16I:
        case TextureFormat::RG16UI:
        case TextureFormat::RGBA8:
        case TextureFormat::RGBA8I:
        case TextureFormat::RGBA8UI:
        case TextureFormat::Depth24:
        case TextureFormat::Depth32F:
        case TextureFormat::Depth24Stencil8:
        case TextureFormat::SRGBA8: return 4;
        case TextureFormat::RGB16I:
        case TextureFormat::RGB16UI:
        case TextureFormat::RGB16F:
        case TextureFormat::RG32F:
        case TextureFormat::RG32I:
        case TextureFormat::RG32UI:
        case TextureFormat::RGBA16F:
        case TextureFormat::RGBA16I:
        case TextureFormat::RGBA16UI:
        case TextureFormat::Depth32FStencil8: return 8;
        case TextureFormat::RGB32F:
        case TextureFormat::RGB32I:
        case TextureFormat::RGB32UI:
        case TextureFormat::RGBA32F:
        case TextureFormat::RGBA32I:
        case TextureFormat::RGBA32UI: return 16;
        default:
        {
            assert(!"Unknown texture format");
            return 0;
        }
    }
}

constexpr size_t format_channel_count(TextureFormat format)
{
    switch (format)
    {
        case TextureFormat::R8:
        case TextureFormat::R8I:
        case TextureFormat::R8UI:
        case TextureFormat::R16F:
        case TextureFormat::R16I:
        case TextureFormat::R16UI:
        case TextureFormat::R32F:
        case TextureFormat::R32I:
        case TextureFormat::R32UI:
        case TextureFormat::Depth16:
        case TextureFormat::Depth24:
        case TextureFormat::Depth32F: return 1;
        case TextureFormat::RG8:
        case TextureFormat::RG8I:
        case TextureFormat::RG8UI:
        case TextureFormat::RG16F:
        case TextureFormat::RG16I:
        case TextureFormat::RG16UI:
        case TextureFormat::RG32F:
        case TextureFormat::RG32I:
        case TextureFormat::RG32UI:
        case TextureFormat::Depth24Stencil8:
        case TextureFormat::Depth32FStencil8: return 2;
        case TextureFormat::RGB8:
        case TextureFormat::RGB8I:
        case TextureFormat::RGB8UI:
        case TextureFormat::RGB16I:
        case TextureFormat::RGB16UI:
        case TextureFormat::RGB16F:
        case TextureFormat::RGB32F:
        case TextureFormat::RGB32I:
        case TextureFormat::RGB32UI:
        case TextureFormat::RGB_BC1:
        case TextureFormat::RGB_ETC1:
        case TextureFormat::RGB_ETC2:
        case TextureFormat::RGB_PVRTC1_4BPP:
        case TextureFormat::SRGB_ETC1:
        case TextureFormat::SRGB_ETC2:
        case TextureFormat::SRGB_PVRTC1_4BPP: return 3;
        case TextureFormat::RGBA8:
        case TextureFormat::RGBA8I:
        case TextureFormat::RGBA8UI:
        case TextureFormat::RGBA16F:
        case TextureFormat::RGBA16I:
        case TextureFormat::RGBA16UI:
        case TextureFormat::RGBA32F:
        case TextureFormat::RGBA32I:
        case TextureFormat::RGBA32UI:
        case TextureFormat::RGBA_BC1:
        case TextureFormat::RGBA_BC2:
        case TextureFormat::RGBA_BC3:
        case TextureFormat::RGBA_BC7:
        case TextureFormat::RGBA_ETC2_EAC:
        case TextureFormat::RGBA_PVRTC1_4BPP:
        case TextureFormat::RGBA_PVRTC2_4BPP:
        case TextureFormat::RGBA_ASTC_4x4:
        case TextureFormat::SRGBA8:
        case TextureFormat::SRGB_BC1:
        case TextureFormat::SRGBA_BC1:
        case TextureFormat::SRGBA_BC2:
        case TextureFormat::SRGBA_BC3:
        case TextureFormat::SRGBA_BC7:
        case TextureFormat::SRGBA_ETC2_EAC:
        case TextureFormat::SRGBA_PVRTC1_4BPP:
        case TextureFormat::SRGBA_PVRTC2_4BPP:
        case TextureFormat::SRGBA_ASTC_4x4: return 4;
        default:
        {
            assert(!"Unknown texture format");
            return 0;
        }
    }
}

constexpr const char* format_str(TextureFormat format)
{
    switch (format)
    {
        case TextureFormat::R8: return "R8";
        case TextureFormat::R8I: return "R8I";
        case TextureFormat::R8UI: return "R8UI";
        case TextureFormat::R16F: return "R16F";
        case TextureFormat::R16I: return "R16I";
        case TextureFormat::R16UI: return "R16UI";
        case TextureFormat::RG8: return "RG8";
        case TextureFormat::RG8I: return "RG8I";
        case TextureFormat::RG8UI: return "RG8UI";
        case TextureFormat::Depth16: return "Depth16";
        case TextureFormat::RGB8: return "RGB8";
        case TextureFormat::RGB8I: return "RGB8I";
        case TextureFormat::RGB8UI: return "RGB8UI";
        case TextureFormat::R32F: return "R32F";
        case TextureFormat::R32I: return "R32I";
        case TextureFormat::R32UI: return "R32UI";
        case TextureFormat::RG16F: return "RG16F";
        case TextureFormat::RG16I: return "RG16I";
        case TextureFormat::RG16UI: return "RG16UI";
        case TextureFormat::RGBA8: return "RGBA8";
        case TextureFormat::RGBA8I: return "RGBA8I";
        case TextureFormat::RGBA8UI: return "RGBA8UI";
        case TextureFormat::Depth24: return "Depth24";
        case TextureFormat::Depth32F: return "Depth32F";
        case TextureFormat::Depth24Stencil8: return "Depth24Stencil8";
        case TextureFormat::RGB16I: return "RGB16I";
        case TextureFormat::RGB16UI: return "RGB16UI";
        case TextureFormat::RGB16F: return "RGB16F";
        case TextureFormat::RG32F: return "RG32F";
        case TextureFormat::RG32I: return "RG32I";
        case TextureFormat::RG32UI: return "RG32UI";
        case TextureFormat::RGBA16F: return "RGBA16F";
        case TextureFormat::RGBA16I: return "RGBA16I";
        case TextureFormat::RGBA16UI: return "RGBA16UI";
        case TextureFormat::Depth32FStencil8: return "Depth32FStencil8";
        case TextureFormat::RGB32F: return "RGB32F";
        case TextureFormat::RGB32I: return "RGB32I";
        case TextureFormat::RGB32UI: return "RGB32UI";
        case TextureFormat::RGBA32F: return "RGBA32F";
        case TextureFormat::RGBA32I: return "RGBA32I";
        case TextureFormat::RGBA32UI: return "RGBA32UI";
        case TextureFormat::RGB_BC1: return "RGB_BC1";
        case TextureFormat::RGBA_BC1: return "RGBA_BC1";
        case TextureFormat::RGBA_BC2: return "RGBA_BC2";
        case TextureFormat::RGBA_BC3: return "RGBA_BC3";
        case TextureFormat::RGBA_BC7: return "RGBA_BC7";
        case TextureFormat::RGB_ETC1: return "RGB_ETC1";
        case TextureFormat::RGB_ETC2: return "RGB_ETC2";
        case TextureFormat::RGBA_ETC2_EAC: return "RGBA_ETC2_EAC";
        case TextureFormat::RGB_PVRTC1_4BPP: return "RGB_PVRTC1_4BPP";
        case TextureFormat::RGBA_PVRTC1_4BPP: return "RGBA_PVRTC1_4BPP";
        case TextureFormat::RGBA_PVRTC2_4BPP: return "RGBA_PVRTC2_4BPP";
        case TextureFormat::RGBA_ASTC_4x4: return "RGBA_ASTC_4x4";
        case TextureFormat::SRGBA8: return "sRGBA8";
        case TextureFormat::SRGB_BC1: return "SRGB_BC1";
        case TextureFormat::SRGBA_BC1: return "SRGBA_BC1";
        case TextureFormat::SRGBA_BC2: return "SRGBA_BC2";
        case TextureFormat::SRGBA_BC3: return "SRGBA_BC3";
        case TextureFormat::SRGBA_BC7: return "SRGBA_BC7";
        case TextureFormat::SRGB_ETC1: return "SRGB_ETC1";
        case TextureFormat::SRGB_ETC2: return "SRGB_ETC2";
        case TextureFormat::SRGBA_ETC2_EAC: return "SRGBA_ETC2_EAC";
        case TextureFormat::SRGB_PVRTC1_4BPP: return "SRGB_PVRTC1_4BPP";
        case TextureFormat::SRGBA_PVRTC1_4BPP: return "SRGBA_PVRTC1_4BPP";
        case TextureFormat::SRGBA_PVRTC2_4BPP: return "SRGBA_PVRTC2_4BPP";
        case TextureFormat::SRGBA_ASTC_4x4: return "SRGBA_ASTC_4x4";
        default:
        {
            return "Unknown format";
        }
    }
}

} // namespace my
