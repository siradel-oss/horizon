#pragma once

#include "mycelium/backend.h"

#include <assert.h>

namespace my
{

template<typename ApplyFunctor>
void apply_vertex_format(VertexFormat format, ApplyFunctor&& func)
{
    switch (format)
    {
        using enum VertexFormat;
        case Int8: std::forward<ApplyFunctor>(func).template apply<int8_t, 1, false>(); break;
        case Int8Norm: std::forward<ApplyFunctor>(func).template apply<int8_t, 1, true>(); break;
        case UInt8: std::forward<ApplyFunctor>(func).template apply<uint8_t, 1, false>(); break;
        case UInt8Norm: std::forward<ApplyFunctor>(func).template apply<uint8_t, 1, true>(); break;
        case Int16: std::forward<ApplyFunctor>(func).template apply<int16_t, 1, false>(); break;
        case Int16Norm: std::forward<ApplyFunctor>(func).template apply<int16_t, 1, true>(); break;
        case UInt16: std::forward<ApplyFunctor>(func).template apply<uint16_t, 1, false>(); break;
        case UInt16Norm:
            std::forward<ApplyFunctor>(func).template apply<uint16_t, 1, true>();
            break;
        case Int32: std::forward<ApplyFunctor>(func).template apply<int32_t, 1, false>(); break;
        case Int32Norm: std::forward<ApplyFunctor>(func).template apply<int32_t, 1, true>(); break;
        case UInt32: std::forward<ApplyFunctor>(func).template apply<uint32_t, 1, false>(); break;
        case UInt32Norm:
            std::forward<ApplyFunctor>(func).template apply<uint32_t, 1, true>();
            break;
        case Float16: std::forward<ApplyFunctor>(func).template apply<float16_t, 1, false>(); break;
        case Float32: std::forward<ApplyFunctor>(func).template apply<float, 1, false>(); break;
        case Int8_2: std::forward<ApplyFunctor>(func).template apply<int8_t, 2, false>(); break;
        case Int8Norm_2: std::forward<ApplyFunctor>(func).template apply<int8_t, 2, true>(); break;
        case UInt8_2: std::forward<ApplyFunctor>(func).template apply<uint8_t, 2, false>(); break;
        case UInt8Norm_2:
            std::forward<ApplyFunctor>(func).template apply<uint8_t, 2, true>();
            break;
        case Int16_2: std::forward<ApplyFunctor>(func).template apply<int16_t, 2, false>(); break;
        case Int16Norm_2:
            std::forward<ApplyFunctor>(func).template apply<int16_t, 2, true>();
            break;
        case UInt16_2: std::forward<ApplyFunctor>(func).template apply<uint16_t, 2, false>(); break;
        case UInt16Norm_2:
            std::forward<ApplyFunctor>(func).template apply<uint16_t, 2, true>();
            break;
        case Int32_2: std::forward<ApplyFunctor>(func).template apply<int32_t, 2, false>(); break;
        case Int32Norm_2:
            std::forward<ApplyFunctor>(func).template apply<int32_t, 2, true>();
            break;
        case UInt32_2: std::forward<ApplyFunctor>(func).template apply<uint32_t, 2, false>(); break;
        case UInt32Norm_2:
            std::forward<ApplyFunctor>(func).template apply<uint32_t, 2, true>();
            break;
        case Float16_2:
            std::forward<ApplyFunctor>(func).template apply<float16_t, 2, false>();
            break;
        case Float32_2: std::forward<ApplyFunctor>(func).template apply<float, 2, false>(); break;
        case Int8_3: std::forward<ApplyFunctor>(func).template apply<int8_t, 3, false>(); break;
        case Int8Norm_3: std::forward<ApplyFunctor>(func).template apply<int8_t, 3, true>(); break;
        case UInt8_3: std::forward<ApplyFunctor>(func).template apply<uint8_t, 3, false>(); break;
        case UInt8Norm_3:
            std::forward<ApplyFunctor>(func).template apply<uint8_t, 3, true>();
            break;
        case Int16_3: std::forward<ApplyFunctor>(func).template apply<int16_t, 3, false>(); break;
        case Int16Norm_3:
            std::forward<ApplyFunctor>(func).template apply<int16_t, 3, true>();
            break;
        case UInt16_3: std::forward<ApplyFunctor>(func).template apply<uint16_t, 3, false>(); break;
        case UInt16Norm_3:
            std::forward<ApplyFunctor>(func).template apply<uint16_t, 3, true>();
            break;
        case Int32_3: std::forward<ApplyFunctor>(func).template apply<int32_t, 3, false>(); break;
        case Int32Norm_3:
            std::forward<ApplyFunctor>(func).template apply<int32_t, 3, true>();
            break;
        case UInt32_3: std::forward<ApplyFunctor>(func).template apply<uint32_t, 3, false>(); break;
        case UInt32Norm_3:
            std::forward<ApplyFunctor>(func).template apply<uint32_t, 3, true>();
            break;
        case Float16_3:
            std::forward<ApplyFunctor>(func).template apply<float16_t, 3, false>();
            break;
        case Float32_3: std::forward<ApplyFunctor>(func).template apply<float, 3, false>(); break;
        case Int8_4: std::forward<ApplyFunctor>(func).template apply<int8_t, 4, false>(); break;
        case Int8Norm_4: std::forward<ApplyFunctor>(func).template apply<int8_t, 4, true>(); break;
        case UInt8_4: std::forward<ApplyFunctor>(func).template apply<uint8_t, 4, false>(); break;
        case UInt8Norm_4:
            std::forward<ApplyFunctor>(func).template apply<uint8_t, 4, true>();
            break;
        case Int16_4: std::forward<ApplyFunctor>(func).template apply<int16_t, 4, false>(); break;
        case Int16Norm_4:
            std::forward<ApplyFunctor>(func).template apply<int16_t, 4, true>();
            break;
        case UInt16_4: std::forward<ApplyFunctor>(func).template apply<uint16_t, 4, false>(); break;
        case UInt16Norm_4:
            std::forward<ApplyFunctor>(func).template apply<uint16_t, 4, true>();
            break;
        case Int32_4: std::forward<ApplyFunctor>(func).template apply<int32_t, 4, false>(); break;
        case Int32Norm_4:
            std::forward<ApplyFunctor>(func).template apply<int32_t, 4, true>();
            break;
        case UInt32_4: std::forward<ApplyFunctor>(func).template apply<uint32_t, 4, false>(); break;
        case UInt32Norm_4:
            std::forward<ApplyFunctor>(func).template apply<uint32_t, 4, true>();
            break;
        case Float16_4:
            std::forward<ApplyFunctor>(func).template apply<float16_t, 4, false>();
            break;
        case Float32_4: std::forward<ApplyFunctor>(func).template apply<float, 4, false>(); break;
    }
}

constexpr VertexFormat to_normalized(VertexFormat format)
{
    switch (format)
    {
        using enum VertexFormat;
        case Int8: return Int8Norm;
        case Int8_2: return Int8Norm_2;
        case Int8_3: return Int8Norm_3;
        case Int8_4: return Int8Norm_4;
        case UInt8: return UInt8Norm;
        case UInt8_2: return UInt8Norm_2;
        case UInt8_3: return UInt8Norm_3;
        case UInt8_4: return UInt8Norm_4;
        case Int16: return Int16Norm;
        case Int16_2: return Int16Norm_2;
        case Int16_3: return Int16Norm_3;
        case Int16_4: return Int16Norm_4;
        case UInt16: return UInt16Norm;
        case UInt16_2: return UInt16Norm_2;
        case UInt16_3: return UInt16Norm_3;
        case UInt16_4: return UInt16Norm_4;
        case Int32: return Int32Norm;
        case Int32_2: return Int32Norm_2;
        case Int32_3: return Int32Norm_3;
        case Int32_4: return Int32Norm_4;
        case UInt32: return UInt32Norm;
        case UInt32_2: return UInt32Norm_2;
        case UInt32_3: return UInt32Norm_3;
        case UInt32_4: return UInt32Norm_4;
        default: return format;
    }
}

constexpr size_t vertex_size(VertexFormat format)
{
    switch (format)
    {
        using enum VertexFormat;
        case Int8:
        case Int8Norm:
        case UInt8:
        case UInt8Norm: return 1;
        case Int16:
        case Int16Norm:
        case UInt16:
        case UInt16Norm:
        case Float16:
        case Int8_2:
        case Int8Norm_2:
        case UInt8_2:
        case UInt8Norm_2: return 2;
        case Int8_3:
        case Int8Norm_3:
        case UInt8_3:
        case UInt8Norm_3: return 3;
        case Int32:
        case Int32Norm:
        case UInt32:
        case UInt32Norm:
        case Float32:
        case Int16_2:
        case Int16Norm_2:
        case UInt16_2:
        case UInt16Norm_2:
        case Float16_2:
        case Int8_4:
        case Int8Norm_4:
        case UInt8_4:
        case UInt8Norm_4: return 4;
        case Int16_3:
        case Int16Norm_3:
        case UInt16_3:
        case UInt16Norm_3:
        case Float16_3: return 6;
        case Int32_2:
        case Int32Norm_2:
        case UInt32_2:
        case UInt32Norm_2:
        case Float32_2:
        case Int16_4:
        case Int16Norm_4:
        case UInt16_4:
        case UInt16Norm_4:
        case Float16_4: return 8;
        case Int32_3:
        case Int32Norm_3:
        case UInt32_3:
        case UInt32Norm_3:
        case Float32_3: return 12;
        case Int32_4:
        case Int32Norm_4:
        case UInt32_4:
        case UInt32Norm_4:
        case Float32_4: return 16;
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
        using enum IndexType;
        case UByte: return 1;
        case UShort: return 2;
        case UInt: return 4;
        default: assert(!"Unknown index type"); return 0;
    }
}

constexpr bool is_format_compressed(TextureFormat format)
{
    switch (format)
    {
        using enum TextureFormat;
        case RGB_BC1:
        case RGBA_BC1:
        case RGBA_BC2:
        case RGBA_BC3:
        case RGBA_BC7:
        case RGB_ETC1:
        case RGB_ETC2:
        case RGBA_ETC2_EAC:
        case RGB_PVRTC1_4BPP:
        case RGBA_PVRTC1_4BPP:
        case RGBA_PVRTC2_4BPP:
        case RGBA_ASTC_4x4:
        case SRGB_BC1:
        case SRGBA_BC1:
        case SRGBA_BC2:
        case SRGBA_BC3:
        case SRGBA_BC7:
        case SRGB_ETC1:
        case SRGB_ETC2:
        case SRGBA_ETC2_EAC:
        case SRGB_PVRTC1_4BPP:
        case SRGBA_PVRTC1_4BPP:
        case SRGBA_PVRTC2_4BPP:
        case SRGBA_ASTC_4x4: return true;
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
        using enum TextureFormat;
        case RGB_BC1:           // Actually 4 bits per pixel
        case RGB_ETC1:          // Actually 4 bits per pixel
        case RGB_ETC2:          // Actually 4 bits per pixel
        case RGB_PVRTC1_4BPP:   // Actually 4 bits per pixel
        case RGBA_PVRTC1_4BPP:  // Actually 4 bits per pixel
        case RGBA_PVRTC2_4BPP:  // Actually 4 bits per pixel
        case SRGB_BC1:          // Actually 4 bits per pixel
        case SRGB_ETC1:         // Actually 4 bits per pixel
        case SRGB_ETC2:         // Actually 4 bits per pixel
        case SRGB_PVRTC1_4BPP:  // Actually 4 bits per pixel
        case SRGBA_PVRTC1_4BPP: // Actually 4 bits per pixel
        case SRGBA_PVRTC2_4BPP: // Actually 4 bits per pixel
            return 1;
        case R8:
        case R8I:
        case R8UI:
        case RGBA_BC1:
        case RGBA_BC2:
        case RGBA_BC3:
        case RGBA_BC7:
        case RGBA_ETC2_EAC:
        case RGBA_ASTC_4x4:
        case SRGBA_BC1:
        case SRGBA_BC2:
        case SRGBA_BC3:
        case SRGBA_BC7:
        case SRGBA_ETC2_EAC:
        case SRGBA_ASTC_4x4: return 1;
        case R16F:
        case R16I:
        case R16UI:
        case RG8:
        case RG8I:
        case RG8UI:
        case Depth16: return 2;
        case RGB8:
        case RGB8I:
        case RGB8UI: return 3;
        case R32F:
        case R32I:
        case R32UI:
        case RG16F:
        case RG16I:
        case RG16UI:
        case RGBA8:
        case RGBA8I:
        case RGBA8UI:
        case SRGBA8:
        case Depth24:
        case Depth32F:
        case Depth24Stencil8: return 4;
        case RGB16I:
        case RGB16UI:
        case RGB16F: return 6;
        case RG32F:
        case RG32I:
        case RG32UI:
        case RGBA16F:
        case RGBA16I:
        case RGBA16UI:
        case Depth32FStencil8: return 8;
        case RGB32F:
        case RGB32I:
        case RGB32UI: return 12;
        case RGBA32F:
        case RGBA32I:
        case RGBA32UI: return 16;
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
        using enum TextureFormat;
        case RGB_BC1:           // Actually 4 bits per pixel
        case RGB_ETC1:          // Actually 4 bits per pixel
        case RGB_ETC2:          // Actually 4 bits per pixel
        case RGB_PVRTC1_4BPP:   // Actually 4 bits per pixel
        case RGBA_PVRTC1_4BPP:  // Actually 4 bits per pixel
        case RGBA_PVRTC2_4BPP:  // Actually 4 bits per pixel
        case SRGB_BC1:          // Actually 4 bits per pixel
        case SRGB_ETC1:         // Actually 4 bits per pixel
        case SRGB_ETC2:         // Actually 4 bits per pixel
        case SRGB_PVRTC1_4BPP:  // Actually 4 bits per pixel
        case SRGBA_PVRTC1_4BPP: // Actually 4 bits per pixel
        case SRGBA_PVRTC2_4BPP: // Actually 4 bits per pixel
            return 1;
        case R8:
        case R8I:
        case R8UI:
        case RGBA_BC1:
        case RGBA_BC2:
        case RGBA_BC3:
        case RGBA_BC7:
        case RGBA_ETC2_EAC:
        case RGBA_ASTC_4x4:
        case SRGBA_BC1:
        case SRGBA_BC2:
        case SRGBA_BC3:
        case SRGBA_BC7:
        case SRGBA_ETC2_EAC:
        case SRGBA_ASTC_4x4: return 1;
        case R16F:
        case R16I:
        case R16UI:
        case RG8:
        case RG8I:
        case RG8UI:
        case Depth16: return 2;
        case RGB8:
        case RGB8I:
        case RGB8UI:
        case R32F:
        case R32I:
        case R32UI:
        case RG16F:
        case RG16I:
        case RG16UI:
        case RGBA8:
        case RGBA8I:
        case RGBA8UI:
        case Depth24:
        case Depth32F:
        case Depth24Stencil8:
        case SRGBA8: return 4;
        case RGB16I:
        case RGB16UI:
        case RGB16F:
        case RG32F:
        case RG32I:
        case RG32UI:
        case RGBA16F:
        case RGBA16I:
        case RGBA16UI:
        case Depth32FStencil8: return 8;
        case RGB32F:
        case RGB32I:
        case RGB32UI:
        case RGBA32F:
        case RGBA32I:
        case RGBA32UI: return 16;
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
        using enum TextureFormat;
        case R8:
        case R8I:
        case R8UI:
        case R16F:
        case R16I:
        case R16UI:
        case R32F:
        case R32I:
        case R32UI:
        case Depth16:
        case Depth24:
        case Depth32F: return 1;
        case RG8:
        case RG8I:
        case RG8UI:
        case RG16F:
        case RG16I:
        case RG16UI:
        case RG32F:
        case RG32I:
        case RG32UI:
        case Depth24Stencil8:
        case Depth32FStencil8: return 2;
        case RGB8:
        case RGB8I:
        case RGB8UI:
        case RGB16I:
        case RGB16UI:
        case RGB16F:
        case RGB32F:
        case RGB32I:
        case RGB32UI:
        case RGB_BC1:
        case RGB_ETC1:
        case RGB_ETC2:
        case RGB_PVRTC1_4BPP:
        case SRGB_ETC1:
        case SRGB_ETC2:
        case SRGB_PVRTC1_4BPP: return 3;
        case RGBA8:
        case RGBA8I:
        case RGBA8UI:
        case RGBA16F:
        case RGBA16I:
        case RGBA16UI:
        case RGBA32F:
        case RGBA32I:
        case RGBA32UI:
        case RGBA_BC1:
        case RGBA_BC2:
        case RGBA_BC3:
        case RGBA_BC7:
        case RGBA_ETC2_EAC:
        case RGBA_PVRTC1_4BPP:
        case RGBA_PVRTC2_4BPP:
        case RGBA_ASTC_4x4:
        case SRGBA8:
        case SRGB_BC1:
        case SRGBA_BC1:
        case SRGBA_BC2:
        case SRGBA_BC3:
        case SRGBA_BC7:
        case SRGBA_ETC2_EAC:
        case SRGBA_PVRTC1_4BPP:
        case SRGBA_PVRTC2_4BPP:
        case SRGBA_ASTC_4x4: return 4;
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
        using enum TextureFormat;
        case R8: return "R8";
        case R8I: return "R8I";
        case R8UI: return "R8UI";
        case R16F: return "R16F";
        case R16I: return "R16I";
        case R16UI: return "R16UI";
        case RG8: return "RG8";
        case RG8I: return "RG8I";
        case RG8UI: return "RG8UI";
        case Depth16: return "Depth16";
        case RGB8: return "RGB8";
        case RGB8I: return "RGB8I";
        case RGB8UI: return "RGB8UI";
        case R32F: return "R32F";
        case R32I: return "R32I";
        case R32UI: return "R32UI";
        case RG16F: return "RG16F";
        case RG16I: return "RG16I";
        case RG16UI: return "RG16UI";
        case RGBA8: return "RGBA8";
        case RGBA8I: return "RGBA8I";
        case RGBA8UI: return "RGBA8UI";
        case Depth24: return "Depth24";
        case Depth32F: return "Depth32F";
        case Depth24Stencil8: return "Depth24Stencil8";
        case RGB16I: return "RGB16I";
        case RGB16UI: return "RGB16UI";
        case RGB16F: return "RGB16F";
        case RG32F: return "RG32F";
        case RG32I: return "RG32I";
        case RG32UI: return "RG32UI";
        case RGBA16F: return "RGBA16F";
        case RGBA16I: return "RGBA16I";
        case RGBA16UI: return "RGBA16UI";
        case Depth32FStencil8: return "Depth32FStencil8";
        case RGB32F: return "RGB32F";
        case RGB32I: return "RGB32I";
        case RGB32UI: return "RGB32UI";
        case RGBA32F: return "RGBA32F";
        case RGBA32I: return "RGBA32I";
        case RGBA32UI: return "RGBA32UI";
        case RGB_BC1: return "RGB_BC1";
        case RGBA_BC1: return "RGBA_BC1";
        case RGBA_BC2: return "RGBA_BC2";
        case RGBA_BC3: return "RGBA_BC3";
        case RGBA_BC7: return "RGBA_BC7";
        case RGB_ETC1: return "RGB_ETC1";
        case RGB_ETC2: return "RGB_ETC2";
        case RGBA_ETC2_EAC: return "RGBA_ETC2_EAC";
        case RGB_PVRTC1_4BPP: return "RGB_PVRTC1_4BPP";
        case RGBA_PVRTC1_4BPP: return "RGBA_PVRTC1_4BPP";
        case RGBA_PVRTC2_4BPP: return "RGBA_PVRTC2_4BPP";
        case RGBA_ASTC_4x4: return "RGBA_ASTC_4x4";
        case SRGBA8: return "sRGBA8";
        case SRGB_BC1: return "SRGB_BC1";
        case SRGBA_BC1: return "SRGBA_BC1";
        case SRGBA_BC2: return "SRGBA_BC2";
        case SRGBA_BC3: return "SRGBA_BC3";
        case SRGBA_BC7: return "SRGBA_BC7";
        case SRGB_ETC1: return "SRGB_ETC1";
        case SRGB_ETC2: return "SRGB_ETC2";
        case SRGBA_ETC2_EAC: return "SRGBA_ETC2_EAC";
        case SRGB_PVRTC1_4BPP: return "SRGB_PVRTC1_4BPP";
        case SRGBA_PVRTC1_4BPP: return "SRGBA_PVRTC1_4BPP";
        case SRGBA_PVRTC2_4BPP: return "SRGBA_PVRTC2_4BPP";
        case SRGBA_ASTC_4x4: return "SRGBA_ASTC_4x4";
        default:
        {
            return "Unknown format";
        }
    }
}

} // namespace my
