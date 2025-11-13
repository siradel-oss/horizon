#include "conv.h"

#include <assert.h>

namespace my
{
GLenum to_gl(PrimitiveType p)
{
    switch (p)
    {
        case PrimitiveType::PointList: return GL_POINTS;
        case PrimitiveType::LineList: return GL_LINES;
        case PrimitiveType::LineStrip: return GL_LINE_STRIP;
        case PrimitiveType::LineLoop: return GL_LINE_LOOP;
        case PrimitiveType::TriangleList: return GL_TRIANGLES;
        case PrimitiveType::TriangleStrip: return GL_TRIANGLE_STRIP;
        case PrimitiveType::TriangleFan: return GL_TRIANGLE_FAN;
        default:
        {
            assert(!"Unknown primitive type");
            return GL_TRIANGLES;
        }
    }
}

GLint format_components(VertexFormat format)
{
    switch (format)
    {
        case VertexFormat::Int8:
        case VertexFormat::Int8Norm:
        case VertexFormat::UInt8:
        case VertexFormat::UInt8Norm:
        case VertexFormat::Int16:
        case VertexFormat::Int16Norm:
        case VertexFormat::UInt16:
        case VertexFormat::UInt16Norm:
        case VertexFormat::Int32:
        case VertexFormat::Int32Norm:
        case VertexFormat::UInt32:
        case VertexFormat::UInt32Norm:
        case VertexFormat::Float16:
        case VertexFormat::Float32: return 1;
        case VertexFormat::Int8_2:
        case VertexFormat::Int8Norm_2:
        case VertexFormat::UInt8_2:
        case VertexFormat::UInt8Norm_2:
        case VertexFormat::Int16_2:
        case VertexFormat::Int16Norm_2:
        case VertexFormat::UInt16_2:
        case VertexFormat::UInt16Norm_2:
        case VertexFormat::Int32_2:
        case VertexFormat::Int32Norm_2:
        case VertexFormat::UInt32_2:
        case VertexFormat::UInt32Norm_2:
        case VertexFormat::Float16_2:
        case VertexFormat::Float32_2: return 2;
        case VertexFormat::Int8_3:
        case VertexFormat::Int8Norm_3:
        case VertexFormat::UInt8_3:
        case VertexFormat::UInt8Norm_3:
        case VertexFormat::Int16_3:
        case VertexFormat::Int16Norm_3:
        case VertexFormat::UInt16_3:
        case VertexFormat::UInt16Norm_3:
        case VertexFormat::Int32_3:
        case VertexFormat::Int32Norm_3:
        case VertexFormat::UInt32_3:
        case VertexFormat::UInt32Norm_3:
        case VertexFormat::Float16_3:
        case VertexFormat::Float32_3: return 3;
        case VertexFormat::Int8_4:
        case VertexFormat::Int8Norm_4:
        case VertexFormat::UInt8_4:
        case VertexFormat::UInt8Norm_4:
        case VertexFormat::Int16_4:
        case VertexFormat::Int16Norm_4:
        case VertexFormat::UInt16_4:
        case VertexFormat::UInt16Norm_4:
        case VertexFormat::Int32_4:
        case VertexFormat::Int32Norm_4:
        case VertexFormat::UInt32_4:
        case VertexFormat::UInt32Norm_4:
        case VertexFormat::Float16_4:
        case VertexFormat::Float32_4: return 4;
        default:
        {
            assert(!"Unknown vertex format");
            return 0;
        }
    }
}

GLenum format_type(VertexFormat format)
{
    switch (format)
    {
        case VertexFormat::Int8:
        case VertexFormat::Int8Norm:
        case VertexFormat::Int8_2:
        case VertexFormat::Int8Norm_2:
        case VertexFormat::Int8_3:
        case VertexFormat::Int8Norm_3:
        case VertexFormat::Int8_4:
        case VertexFormat::Int8Norm_4: return GL_BYTE;
        case VertexFormat::UInt8:
        case VertexFormat::UInt8Norm:
        case VertexFormat::UInt8_2:
        case VertexFormat::UInt8Norm_2:
        case VertexFormat::UInt8_3:
        case VertexFormat::UInt8Norm_3:
        case VertexFormat::UInt8_4:
        case VertexFormat::UInt8Norm_4: return GL_UNSIGNED_BYTE;
        case VertexFormat::Int16:
        case VertexFormat::Int16Norm:
        case VertexFormat::Int16_2:
        case VertexFormat::Int16Norm_2:
        case VertexFormat::Int16_3:
        case VertexFormat::Int16Norm_3:
        case VertexFormat::Int16_4:
        case VertexFormat::Int16Norm_4: return GL_SHORT;
        case VertexFormat::UInt16:
        case VertexFormat::UInt16Norm:
        case VertexFormat::UInt16_2:
        case VertexFormat::UInt16Norm_2:
        case VertexFormat::UInt16_3:
        case VertexFormat::UInt16Norm_3:
        case VertexFormat::UInt16_4:
        case VertexFormat::UInt16Norm_4: return GL_UNSIGNED_SHORT;
        case VertexFormat::Int32:
        case VertexFormat::Int32Norm:
        case VertexFormat::Int32_2:
        case VertexFormat::Int32Norm_2:
        case VertexFormat::Int32_3:
        case VertexFormat::Int32Norm_3:
        case VertexFormat::Int32_4:
        case VertexFormat::Int32Norm_4: return GL_INT;
        case VertexFormat::UInt32:
        case VertexFormat::UInt32Norm:
        case VertexFormat::UInt32_2:
        case VertexFormat::UInt32Norm_2:
        case VertexFormat::UInt32_3:
        case VertexFormat::UInt32Norm_3:
        case VertexFormat::UInt32_4:
        case VertexFormat::UInt32Norm_4: return GL_UNSIGNED_INT;
        case VertexFormat::Float16:
        case VertexFormat::Float16_2:
        case VertexFormat::Float16_3:
        case VertexFormat::Float16_4: return GL_HALF_FLOAT;
        case VertexFormat::Float32:
        case VertexFormat::Float32_2:
        case VertexFormat::Float32_3:
        case VertexFormat::Float32_4: return GL_FLOAT;
        default:
        {
            assert(!"Unknown vertex format");
            return 0;
        }
    }
}

bool format_normalized(VertexFormat format)
{
    switch (format)
    {
        case VertexFormat::Int8:
        case VertexFormat::UInt8:
        case VertexFormat::Int16:
        case VertexFormat::UInt16:
        case VertexFormat::Int32:
        case VertexFormat::UInt32:
        case VertexFormat::Int8_2:
        case VertexFormat::UInt8_2:
        case VertexFormat::Int16_2:
        case VertexFormat::UInt16_2:
        case VertexFormat::Int32_2:
        case VertexFormat::UInt32_2:
        case VertexFormat::Int8_3:
        case VertexFormat::UInt8_3:
        case VertexFormat::Int16_3:
        case VertexFormat::UInt16_3:
        case VertexFormat::Int32_3:
        case VertexFormat::UInt32_3:
        case VertexFormat::Int8_4:
        case VertexFormat::UInt8_4:
        case VertexFormat::Int16_4:
        case VertexFormat::UInt16_4:
        case VertexFormat::Int32_4:
        case VertexFormat::UInt32_4: return false;
        case VertexFormat::Int8Norm:
        case VertexFormat::UInt8Norm:
        case VertexFormat::Int16Norm:
        case VertexFormat::UInt16Norm:
        case VertexFormat::Int32Norm:
        case VertexFormat::UInt32Norm:
        case VertexFormat::Int8Norm_2:
        case VertexFormat::UInt8Norm_2:
        case VertexFormat::Int16Norm_2:
        case VertexFormat::UInt16Norm_2:
        case VertexFormat::Int32Norm_2:
        case VertexFormat::UInt32Norm_2:
        case VertexFormat::Int8Norm_3:
        case VertexFormat::UInt8Norm_3:
        case VertexFormat::Int16Norm_3:
        case VertexFormat::UInt16Norm_3:
        case VertexFormat::Int32Norm_3:
        case VertexFormat::UInt32Norm_3:
        case VertexFormat::Int8Norm_4:
        case VertexFormat::UInt8Norm_4:
        case VertexFormat::Int16Norm_4:
        case VertexFormat::UInt16Norm_4:
        case VertexFormat::Int32Norm_4:
        case VertexFormat::UInt32Norm_4: return true;
        case VertexFormat::Float16:
        case VertexFormat::Float32:
        case VertexFormat::Float16_2:
        case VertexFormat::Float32_2:
        case VertexFormat::Float16_3:
        case VertexFormat::Float32_3:
        case VertexFormat::Float16_4:
        case VertexFormat::Float32_4: return false;
        default:
        {
            assert(!"Unknown vertex format");
            return false;
        }
    }
}

bool format_integer(VertexFormat format)
{
    switch (format)
    {
        case VertexFormat::Int8:
        case VertexFormat::UInt8:
        case VertexFormat::Int16:
        case VertexFormat::UInt16:
        case VertexFormat::Int32:
        case VertexFormat::UInt32:
        case VertexFormat::Int8_2:
        case VertexFormat::UInt8_2:
        case VertexFormat::Int16_2:
        case VertexFormat::UInt16_2:
        case VertexFormat::Int32_2:
        case VertexFormat::UInt32_2:
        case VertexFormat::Int8_3:
        case VertexFormat::UInt8_3:
        case VertexFormat::Int16_3:
        case VertexFormat::UInt16_3:
        case VertexFormat::Int32_3:
        case VertexFormat::UInt32_3:
        case VertexFormat::Int8_4:
        case VertexFormat::UInt8_4:
        case VertexFormat::Int16_4:
        case VertexFormat::UInt16_4:
        case VertexFormat::Int32_4:
        case VertexFormat::UInt32_4: return true;
        case VertexFormat::Int8Norm:
        case VertexFormat::UInt8Norm:
        case VertexFormat::Int16Norm:
        case VertexFormat::UInt16Norm:
        case VertexFormat::Int32Norm:
        case VertexFormat::UInt32Norm:
        case VertexFormat::Int8Norm_2:
        case VertexFormat::UInt8Norm_2:
        case VertexFormat::Int16Norm_2:
        case VertexFormat::UInt16Norm_2:
        case VertexFormat::Int32Norm_2:
        case VertexFormat::UInt32Norm_2:
        case VertexFormat::Int8Norm_3:
        case VertexFormat::UInt8Norm_3:
        case VertexFormat::Int16Norm_3:
        case VertexFormat::UInt16Norm_3:
        case VertexFormat::Int32Norm_3:
        case VertexFormat::UInt32Norm_3:
        case VertexFormat::Int8Norm_4:
        case VertexFormat::UInt8Norm_4:
        case VertexFormat::Int16Norm_4:
        case VertexFormat::UInt16Norm_4:
        case VertexFormat::Int32Norm_4:
        case VertexFormat::UInt32Norm_4: return false;
        case VertexFormat::Float16:
        case VertexFormat::Float32:
        case VertexFormat::Float16_2:
        case VertexFormat::Float32_2:
        case VertexFormat::Float16_3:
        case VertexFormat::Float32_3:
        case VertexFormat::Float16_4:
        case VertexFormat::Float32_4: return false;
        default:
        {
            assert(!"Unknown vertex format");
            return false;
        }
    }
}

size_t format_format(TextureFormat format)
{
    switch (format)
    {
        case TextureFormat::R8:
        case TextureFormat::R16F:
        case TextureFormat::R32F: return GL_RED;
        case TextureFormat::R8I:
        case TextureFormat::R8UI:
        case TextureFormat::R16I:
        case TextureFormat::R16UI:
        case TextureFormat::R32I:
        case TextureFormat::R32UI: return GL_RED_INTEGER;
        case TextureFormat::RG8:
        case TextureFormat::RG16F:
        case TextureFormat::RG32F: return GL_RG;
        case TextureFormat::RG8I:
        case TextureFormat::RG8UI:
        case TextureFormat::RG16I:
        case TextureFormat::RG16UI:
        case TextureFormat::RG32I:
        case TextureFormat::RG32UI: return GL_RG_INTEGER;
        case TextureFormat::RGB8:
        case TextureFormat::RGB16F:
        case TextureFormat::RGB32F: return GL_RGB;
        case TextureFormat::RGB8I:
        case TextureFormat::RGB8UI:
        case TextureFormat::RGB16I:
        case TextureFormat::RGB16UI:
        case TextureFormat::RGB32I:
        case TextureFormat::RGB32UI: return GL_RGB_INTEGER;
        case TextureFormat::RGBA8:
        case TextureFormat::RGBA16F:
        case TextureFormat::RGBA32F:
        case TextureFormat::SRGBA8: return GL_RGBA;
        case TextureFormat::RGBA8I:
        case TextureFormat::RGBA8UI:
        case TextureFormat::RGBA16I:
        case TextureFormat::RGBA16UI:
        case TextureFormat::RGBA32I:
        case TextureFormat::RGBA32UI: return GL_RGBA_INTEGER;
        case TextureFormat::Depth16:
        case TextureFormat::Depth24:
        case TextureFormat::Depth32F: return GL_DEPTH_COMPONENT;
        case TextureFormat::Depth24Stencil8:
        case TextureFormat::Depth32FStencil8: return GL_DEPTH_STENCIL;
        default:
        {
            assert(!"Unknown texture format");
            return 0;
        }
    }
}

size_t format_type(TextureFormat format)
{
    switch (format)
    {
        case TextureFormat::R8I:
        case TextureFormat::RG8I:
        case TextureFormat::RGB8I:
        case TextureFormat::RGBA8I: return GL_BYTE;
        case TextureFormat::R8:
        case TextureFormat::R8UI:
        case TextureFormat::RG8:
        case TextureFormat::RG8UI:
        case TextureFormat::RGB8:
        case TextureFormat::RGB8UI:
        case TextureFormat::RGBA8:
        case TextureFormat::RGBA8UI:
        case TextureFormat::SRGBA8: return GL_UNSIGNED_BYTE;
        case TextureFormat::R16I:
        case TextureFormat::RG16I:
        case TextureFormat::RGB16I:
        case TextureFormat::RGBA16I: return GL_SHORT;
        case TextureFormat::R16UI:
        case TextureFormat::RG16UI:
        case TextureFormat::RGB16UI:
        case TextureFormat::RGBA16UI:
        case TextureFormat::Depth16: return GL_UNSIGNED_SHORT;
        case TextureFormat::R32I:
        case TextureFormat::RG32I:
        case TextureFormat::RGB32I:
        case TextureFormat::RGBA32I: return GL_INT;
        case TextureFormat::R32UI:
        case TextureFormat::RG32UI:
        case TextureFormat::RGB32UI:
        case TextureFormat::RGBA32UI:
        case TextureFormat::Depth24: return GL_UNSIGNED_INT;
        case TextureFormat::Depth24Stencil8: return GL_UNSIGNED_INT_24_8;
        case TextureFormat::R16F:
        case TextureFormat::RG16F:
        case TextureFormat::RGB16F:
        case TextureFormat::RGBA16F: return GL_HALF_FLOAT;
        case TextureFormat::R32F:
        case TextureFormat::RG32F:
        case TextureFormat::RGB32F:
        case TextureFormat::RGBA32F:
        case TextureFormat::Depth32F: return GL_FLOAT;
        case TextureFormat::Depth32FStencil8: return GL_FLOAT_32_UNSIGNED_INT_24_8_REV;
        default:
        {
            assert(!"Unknown texture format");
            return 0;
        }
    }
}

size_t format_internal(TextureFormat format)
{
    switch (format)
    {
        case TextureFormat::R8I: return GL_R8I;
        case TextureFormat::RG8I: return GL_RG8I;
        case TextureFormat::RGB8I: return GL_RGB8I;
        case TextureFormat::RGBA8I: return GL_RGBA8I;
        case TextureFormat::R8: return GL_R8;
        case TextureFormat::R8UI: return GL_R8UI;
        case TextureFormat::RG8: return GL_RG8;
        case TextureFormat::RG8UI: return GL_RG8UI;
        case TextureFormat::RGB8: return GL_RGB8;
        case TextureFormat::RGB8UI: return GL_RGB8UI;
        case TextureFormat::RGBA8: return GL_RGBA8;
        case TextureFormat::RGBA8UI: return GL_RGBA8UI;
        case TextureFormat::R16I: return GL_R16I;
        case TextureFormat::RG16I: return GL_RG16I;
        case TextureFormat::RGB16I: return GL_RGB16I;
        case TextureFormat::RGBA16I: return GL_RGBA16I;
        case TextureFormat::R16UI: return GL_R16UI;
        case TextureFormat::RG16UI: return GL_RG16UI;
        case TextureFormat::RGB16UI: return GL_RGB16UI;
        case TextureFormat::RGBA16UI: return GL_RGBA16UI;
        case TextureFormat::Depth16: return GL_DEPTH_COMPONENT16;
        case TextureFormat::R32I: return GL_R32I;
        case TextureFormat::RG32I: return GL_RG32I;
        case TextureFormat::RGB32I: return GL_RGB32I;
        case TextureFormat::RGBA32I: return GL_RGBA32I;
        case TextureFormat::R32UI: return GL_R32UI;
        case TextureFormat::RG32UI: return GL_RG32UI;
        case TextureFormat::RGB32UI: return GL_RGB32UI;
        case TextureFormat::RGBA32UI: return GL_RGBA32UI;
        case TextureFormat::Depth24: return GL_DEPTH_COMPONENT24;
        case TextureFormat::Depth24Stencil8: return GL_DEPTH24_STENCIL8;
        case TextureFormat::R16F: return GL_R16F;
        case TextureFormat::RG16F: return GL_RG16F;
        case TextureFormat::RGB16F: return GL_RGB16F;
        case TextureFormat::RGBA16F: return GL_RGBA16F;
        case TextureFormat::R32F: return GL_R32F;
        case TextureFormat::RG32F: return GL_RG32F;
        case TextureFormat::RGB32F: return GL_RGB32F;
        case TextureFormat::RGBA32F: return GL_RGBA32F;
        case TextureFormat::Depth32F: return GL_DEPTH_COMPONENT32F;
        case TextureFormat::Depth32FStencil8: return GL_DEPTH32F_STENCIL8;
        case TextureFormat::RGB_BC1: return GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
        case TextureFormat::RGBA_BC1: return GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
        case TextureFormat::RGBA_BC2: return GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
        case TextureFormat::RGBA_BC3: return GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
        case TextureFormat::RGBA_BC7: return GL_COMPRESSED_RGBA_BPTC_UNORM_ARB; // _EXT in OpenGL ES
        case TextureFormat::RGB_ETC1: return GL_ETC1_RGB8_OES;
        case TextureFormat::RGB_ETC2: return GL_COMPRESSED_RGB8_ETC2;
        case TextureFormat::RGBA_ETC2_EAC: return GL_COMPRESSED_RGBA8_ETC2_EAC;
        case TextureFormat::RGB_PVRTC1_4BPP: return GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG;
        case TextureFormat::RGBA_PVRTC1_4BPP: return GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG;
        case TextureFormat::RGBA_PVRTC2_4BPP: return GL_COMPRESSED_RGBA_PVRTC_4BPPV2_IMG;
        case TextureFormat::RGBA_ASTC_4x4: return GL_COMPRESSED_RGBA_ASTC_4x4_KHR;
        case TextureFormat::SRGBA8: return GL_SRGB8_ALPHA8;
        case TextureFormat::SRGB_BC1: return GL_COMPRESSED_SRGB_S3TC_DXT1_EXT;
        case TextureFormat::SRGBA_BC1: return GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT;
        case TextureFormat::SRGBA_BC2: return GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT;
        case TextureFormat::SRGBA_BC3: return GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT;
        case TextureFormat::SRGBA_BC7:
            return GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM_ARB; // _EXT in OpenGL ES
        case TextureFormat::SRGB_ETC1:
            return GL_COMPRESSED_SRGB8_ETC2; // There's no separate SRGB ETC1 format in GL
        case TextureFormat::SRGB_ETC2: return GL_COMPRESSED_SRGB8_ETC2;
        case TextureFormat::SRGBA_ETC2_EAC: return GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC;
        case TextureFormat::SRGB_PVRTC1_4BPP: return GL_COMPRESSED_SRGB_PVRTC_4BPPV1_EXT;
        case TextureFormat::SRGBA_PVRTC1_4BPP: return GL_COMPRESSED_SRGB_ALPHA_PVRTC_4BPPV1_EXT;
        case TextureFormat::SRGBA_PVRTC2_4BPP: return GL_COMPRESSED_SRGB_ALPHA_PVRTC_4BPPV2_IMG;
        case TextureFormat::SRGBA_ASTC_4x4: return GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR;
        default:
        {
            assert(!"Unknown texture format");
            return 0;
        }
    }
}

GLenum to_gl(IndexType type)
{
    switch (type)
    {
        case IndexType::UByte: return GL_UNSIGNED_BYTE;
        case IndexType::UShort: return GL_UNSIGNED_SHORT;
        case IndexType::UInt: return GL_UNSIGNED_INT;
        default: assert(!"Unknown index type"); return 0;
    }
}

GLenum to_gl(UsageHint usage)
{
    switch (usage)
    {
        case UsageHint::Static: return GL_STATIC_DRAW;
        case UsageHint::Updatable:
        case UsageHint::Dynamic: return GL_DYNAMIC_DRAW;
        case UsageHint::Download: return GL_STREAM_READ;
        default: assert(!"We forgot to implement a usage hint"); return GL_STATIC_DRAW;
    }
}

GLenum to_gl(TextureLayout::Type type)
{
    switch (type)
    {
        case TextureLayout::Type2D: return GL_TEXTURE_2D;
        case TextureLayout::Type3D: return GL_TEXTURE_3D;
        case TextureLayout::Array: return GL_TEXTURE_2D_ARRAY;
        default: assert(!"Unknown texture type"); return 0;
    }
}

GLenum to_gl(SamplerParams::Wrap wrap)
{
    switch (wrap)
    {
        case SamplerParams::Wrap::Clamp: return GL_CLAMP_TO_EDGE;
        case SamplerParams::Wrap::Mirror: return GL_MIRRORED_REPEAT;
        case SamplerParams::Wrap::Repeat: return GL_REPEAT;
        default: assert(!"Unknown sampler wrap mode"); return 0;
    }
}

GLenum to_gl(SamplerParams::Filter filter, SamplerParams::Filter mip_filter, bool use_mipmaps)
{
    if (!use_mipmaps)
    {
        switch (filter)
        {
            case SamplerParams::Filter::Nearest: return GL_NEAREST;
            case SamplerParams::Filter::Linear: return GL_LINEAR;
            default: assert(!"Unknown sampler filter mode"); return 0;
        }
    }
    else
    {
        if (mip_filter == SamplerParams::Filter::Nearest)
        {
            switch (filter)
            {
                case SamplerParams::Filter::Nearest: return GL_NEAREST_MIPMAP_NEAREST;
                case SamplerParams::Filter::Linear: return GL_LINEAR_MIPMAP_NEAREST;
                default: assert(!"Unknown sampler filter mode"); return 0;
            }
        }
        else if (mip_filter == SamplerParams::Filter::Linear)
        {
            switch (filter)
            {
                case SamplerParams::Filter::Nearest: return GL_NEAREST_MIPMAP_LINEAR;
                case SamplerParams::Filter::Linear: return GL_LINEAR_MIPMAP_LINEAR;
                default: assert(!"Unknown sampler filter mode"); return 0;
            }
        }
        else
        {
            assert(!"Unknown sampler filter mode");
            return 0;
        }
    }
}

GLenum to_gl(my::RasterizationState::CullMode mode)
{
    switch (mode)
    {
        case my::RasterizationState::Front: return GL_FRONT;
        case my::RasterizationState::Back: return GL_BACK;
        case my::RasterizationState::FrontAndBack: return GL_FRONT_AND_BACK;
        default: return GL_BACK;
    }
}

GLenum to_gl(my::RasterizationState::FrontFace face)
{
    switch (face)
    {
        case my::RasterizationState::Clockwise: return GL_CW;
        case my::RasterizationState::CounterClockwise: return GL_CCW;
        default: return GL_CCW;
    }
}

GLenum to_gl(my::DepthState::Compare func)
{
    switch (func)
    {
        case my::DepthState::Never: return GL_NEVER;
        case my::DepthState::Always: return GL_ALWAYS;
        case my::DepthState::Less: return GL_LESS;
        case my::DepthState::LessEqual: return GL_LEQUAL;
        case my::DepthState::Equal: return GL_EQUAL;
        case my::DepthState::NotEqual: return GL_NOTEQUAL;
        case my::DepthState::Greater: return GL_GREATER;
        case my::DepthState::GreaterEqual: return GL_GEQUAL;
        default: return GL_LESS;
    }
}

GLenum to_gl(my::StencilState::Compare func)
{
    switch (func)
    {
        case my::StencilState::Never: return GL_NEVER;
        case my::StencilState::Always: return GL_ALWAYS;
        case my::StencilState::Less: return GL_LESS;
        case my::StencilState::LessEqual: return GL_LEQUAL;
        case my::StencilState::Equal: return GL_EQUAL;
        case my::StencilState::NotEqual: return GL_NOTEQUAL;
        case my::StencilState::Greater: return GL_GREATER;
        case my::StencilState::GreaterEqual: return GL_GEQUAL;
        default: return GL_LESS;
    }
}

GLenum to_gl(my::StencilState::Op op)
{
    switch (op)
    {
        case my::StencilState::Keep: return GL_KEEP;
        case my::StencilState::Zero: return GL_ZERO;
        case my::StencilState::Replace: return GL_REPLACE;
        case my::StencilState::IncrementClamp: return GL_INCR;
        case my::StencilState::DecrementClamp: return GL_DECR;
        case my::StencilState::Invert: return GL_INVERT;
        case my::StencilState::IncrementWrap: return GL_INCR_WRAP;
        case my::StencilState::DecrementWrap: return GL_DECR_WRAP;
        default: return GL_KEEP;
    }
}

GLenum to_gl(ColorBlendState::Op op)
{
    switch (op)
    {
        case ColorBlendState::Add: return GL_FUNC_ADD;
        case ColorBlendState::Subtract: return GL_FUNC_SUBTRACT;
        case ColorBlendState::ReverseSubtract: return GL_FUNC_REVERSE_SUBTRACT;
        case ColorBlendState::Min: return GL_MIN;
        case ColorBlendState::Max: return GL_MAX;
        default: return GL_FUNC_ADD;
    }
}

GLenum to_gl(ColorBlendState::Factor factor)
{
    switch (factor)
    {
        case ColorBlendState::Zero: return GL_ZERO;
        case ColorBlendState::One: return GL_ONE;
        case ColorBlendState::SrcColor: return GL_SRC_COLOR;
        case ColorBlendState::OneMinusSrcColor: return GL_ONE_MINUS_SRC_COLOR;
        case ColorBlendState::DstColor: return GL_DST_COLOR;
        case ColorBlendState::OneMinusDstColor: return GL_ONE_MINUS_DST_COLOR;
        case ColorBlendState::ConstantColor: return GL_CONSTANT_COLOR;
        case ColorBlendState::SrcAlpha: return GL_SRC_ALPHA;
        case ColorBlendState::OneMinusSrcAlpha: return GL_ONE_MINUS_SRC_ALPHA;
        case ColorBlendState::DstAlpha: return GL_DST_ALPHA;
        case ColorBlendState::OneMinusDstAlpha: return GL_ONE_MINUS_DST_ALPHA;
        case ColorBlendState::ConstantAlpha: return GL_CONSTANT_ALPHA;
        default: return GL_ONE;
    }
}

GLenum to_gl(Attachment a)
{
    switch (a)
    {
        case Attachment::Depth: return GL_DEPTH_ATTACHMENT;
        case Attachment::Stencil: return GL_STENCIL_ATTACHMENT;
        case Attachment::DepthStencil: return GL_DEPTH_STENCIL_ATTACHMENT;
        case Attachment::Color0: return GL_COLOR_ATTACHMENT0;
        case Attachment::Color1: return GL_COLOR_ATTACHMENT1;
        case Attachment::Color2: return GL_COLOR_ATTACHMENT2;
        case Attachment::Color3: return GL_COLOR_ATTACHMENT3;
        case Attachment::Color4: return GL_COLOR_ATTACHMENT4;
        case Attachment::Color5: return GL_COLOR_ATTACHMENT5;
        default: return GL_NONE;
    }
}

GLbitfield to_gl(AspectFlags aspects)
{
    GLbitfield gl = 0;

    if (aspects & Aspect_Color)
    {
        gl |= GL_COLOR_BUFFER_BIT;
    }

    if (aspects & Aspect_Depth)
    {
        gl |= GL_DEPTH_BUFFER_BIT;
    }

    if (aspects & Aspect_Stencil)
    {
        gl |= GL_STENCIL_BUFFER_BIT;
    }

    return gl;
}

GLenum buffer_resource_type_to_gl_target(Resource::Type type)
{
    switch (type)
    {
        case Resource::Type::VertexBuffer: return GL_ARRAY_BUFFER;
        case Resource::Type::IndexBuffer: return GL_ELEMENT_ARRAY_BUFFER;
        case Resource::Type::UniformBuffer: return GL_UNIFORM_BUFFER;
        case Resource::Type::TextureDownloadBuffer: return GL_PIXEL_PACK_BUFFER;
        default: assert(!"Wrong buffer type"); return 0;
    }
}

TextureFormat to_texture_format(TextureDownloadFormat format)
{
    switch (format)
    {
        case TextureDownloadFormat::RGBA8Norm: return TextureFormat::RGBA8;
        case TextureDownloadFormat::RGBA32F: return TextureFormat::RGBA32F;
        case TextureDownloadFormat::RGBA32I: return TextureFormat::RGBA32I;
        case TextureDownloadFormat::RGBA32UI: return TextureFormat::RGBA32UI;
        default: assert(!"Unhandled texture download format."); return TextureFormat::RGBA8;
    }
}

GLenum dl_format_format(TextureDownloadFormat format)
{
    switch (format)
    {
        case TextureDownloadFormat::RGBA8Norm: return GL_RGBA;
        case TextureDownloadFormat::RGBA32F: return GL_RGBA;
        case TextureDownloadFormat::RGBA32I: return GL_RGBA_INTEGER;
        case TextureDownloadFormat::RGBA32UI: return GL_RGBA_INTEGER;
        default: assert(!"Unhandled texture download format."); return GL_RGBA;
    }
}

GLenum dl_format_type(TextureDownloadFormat format)
{
    switch (format)
    {
        case TextureDownloadFormat::RGBA8Norm: return GL_UNSIGNED_BYTE;
        case TextureDownloadFormat::RGBA32F: return GL_FLOAT;
        case TextureDownloadFormat::RGBA32I: return GL_INT;
        case TextureDownloadFormat::RGBA32UI: return GL_UNSIGNED_INT;
        default: assert(!"Unhandled texture download format."); return GL_UNSIGNED_BYTE;
    }
}

} // namespace my
