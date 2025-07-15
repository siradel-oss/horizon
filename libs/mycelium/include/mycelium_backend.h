#pragma once

#include "mycelium_types.h"

#include <assert.h>
#include <gsl/gsl-lite.hpp>

#include <memory>
#include <string>

namespace my
{
enum
{
    MaxVertexAttributes = 15,
    MaxVertexUniformBlocks = 12,
    MaxFragmentUniformBlocks = 12,
    MaxUniformBlocks = 24,
    MaxUniformBlockSize = 16384,
    MaxVertexTextureUnits = 16,
    MaxFragmentTextureUnits = 16,
    MaxTextureUnits = 32,
    MaxFramebufferAttachments = 8,
};

struct ViewportState
{
    Rect viewport;
    Rect scissor;
};

struct RasterizationState
{
    enum CullMode
    {
        None = 0,
        Front = 1,
        Back = 2,
        FrontAndBack = 3,
    };

    enum FrontFace
    {
        Clockwise,
        CounterClockwise,
    };

    CullMode cull_mode = Back;
    FrontFace front_face = CounterClockwise;
    float depth_bias_factor = 0.0f;
    float depth_bias_units = 0.0f;
    float line_width = 1.0f;
};

struct DepthState
{
    enum Compare
    {
        Never,
        Always,
        Less,
        LessEqual,
        Equal,
        NotEqual,
        Greater,
        GreaterEqual,
    };

    bool test = true;
    bool write = true;
    Compare compare = Less;
};

struct StencilState
{
    enum Compare
    {
        Never,
        Always,
        Less,
        LessEqual,
        Equal,
        NotEqual,
        Greater,
        GreaterEqual,
    };

    enum Op
    {
        Keep,
        Zero,
        Replace,
        IncrementClamp,
        DecrementClamp,
        Invert,
        IncrementWrap,
        DecrementWrap,
    };

    struct Face
    {
        Op fail_op = Keep;
        Op depth_pass_op = Keep;
        Op depth_fail_op = Keep;
        Compare compare = Never;
        uint32_t compare_mask = 0;
        uint32_t write_mask = 0;
        int32_t reference = 0;
    };

    bool enable = false;
    Face front;
    Face back;
};

struct ColorBlendState
{
    enum Components
    {
        R = 1,
        G = 2,
        B = 4,
        A = 8,
        RGB = R | G | B,
        RGBA = R | G | B | A,
    };

    enum Factor
    {
        Zero,
        One,
        SrcColor,
        OneMinusSrcColor,
        DstColor,
        OneMinusDstColor,
        ConstantColor,
        SrcAlpha,
        OneMinusSrcAlpha,
        DstAlpha,
        OneMinusDstAlpha,
        ConstantAlpha,
    };

    enum Op
    {
        Add,
        Subtract,
        ReverseSubtract,
        Min,
        Max,
    };

    struct Channel
    {
        Factor src = One;
        Factor dst = Zero;
        Op op = Add;
    };

    bool enable = false;
    Color constant_color;
    Components mask = RGBA;
    Channel color;
    Channel alpha;
};

struct PipelineState
{
    RasterizationState rasterization;
    DepthState depth;
    StencilState stencil;
    ColorBlendState color_blend;
};

struct ResourceHandle
{
    uint64_t handle = 0;

    static ResourceHandle null() { return ResourceHandle{0}; }

    constexpr bool is_null() const { return handle == 0; }

    constexpr operator bool() const { return handle; }

    constexpr bool operator==(const ResourceHandle& h) const { return h.handle == handle; }

    constexpr bool operator!=(const ResourceHandle& h) const { return h.handle != handle; }
};

enum class IndexType
{
    UByte,
    UShort,
    UInt,
};

enum class VertexFormat
{
    Int8,
    Int8Norm,
    UInt8,
    UInt8Norm,
    Int16,
    Int16Norm,
    UInt16,
    UInt16Norm,
    Int32,
    Int32Norm,
    UInt32,
    UInt32Norm,
    Float16,
    Float32,
    Int8_2,
    Int8Norm_2,
    UInt8_2,
    UInt8Norm_2,
    Int16_2,
    Int16Norm_2,
    UInt16_2,
    UInt16Norm_2,
    Int32_2,
    Int32Norm_2,
    UInt32_2,
    UInt32Norm_2,
    Float16_2,
    Float32_2,
    Int8_3,
    Int8Norm_3,
    UInt8_3,
    UInt8Norm_3,
    Int16_3,
    Int16Norm_3,
    UInt16_3,
    UInt16Norm_3,
    Int32_3,
    Int32Norm_3,
    UInt32_3,
    UInt32Norm_3,
    Float16_3,
    Float32_3,
    Int8_4,
    Int8Norm_4,
    UInt8_4,
    UInt8Norm_4,
    Int16_4,
    Int16Norm_4,
    UInt16_4,
    UInt16Norm_4,
    Int32_4,
    Int32Norm_4,
    UInt32_4,
    UInt32Norm_4,
    Float16_4,
    Float32_4,
};

static VertexFormat to_normalized(VertexFormat format)
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

static size_t vertex_size(VertexFormat format)
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

static size_t index_size(IndexType type)
{
    switch (type)
    {
        case IndexType::UByte: return 1;
        case IndexType::UShort: return 2;
        case IndexType::UInt: return 4;
        default: assert(!"Unknown index type"); return 0;
    }
}

enum class TextureFormat
{
    R8,
    R8I,
    R8UI,
    R16F,
    R16I,
    R16UI,
    R32F,
    R32I,
    R32UI,
    RG8,
    RG8I,
    RG8UI,
    RG16F,
    RG16I,
    RG16UI,
    RG32F,
    RG32I,
    RG32UI,
    RGB8,
    RGB8I,
    RGB8UI,
    RGB16F,
    RGB16I,
    RGB16UI,
    RGB32F,
    RGB32I,
    RGB32UI,
    RGBA8,
    RGBA8I,
    RGBA8UI,
    RGBA16F,
    RGBA16I,
    RGBA16UI,
    RGBA32F,
    RGBA32I,
    RGBA32UI,
    Depth16,
    Depth24,
    Depth32F,
    Depth24Stencil8,
    Depth32FStencil8,
    RGB_BC1,  // aka DXT1
    RGBA_BC1, // aka DXT1
    RGBA_BC2, // aka DXT2 and DXT3
    RGBA_BC3, // aka DXT4 and DXT5
    RGBA_BC7,
    RGB_ETC1,
    RGB_ETC2,
    RGBA_ETC2_EAC,
    RGB_PVRTC1_4BPP,
    RGBA_PVRTC1_4BPP,
    RGBA_PVRTC2_4BPP,
    RGBA_ASTC_4x4,
};

static constexpr bool is_format_compressed(TextureFormat format)
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
        case TextureFormat::RGBA_ASTC_4x4: return true;
        default: return false;
    }
}

// Don't use this function to compute the byte size of textures, because
// of compressed formats. Instead, create a TextureLayout and use its
// method for computing the size.
static constexpr size_t format_external_pixel_byte_size(TextureFormat format)
{
    switch (format)
    {
        case TextureFormat::RGB_BC1:          // Actually 4 bits per pixel
        case TextureFormat::RGB_ETC1:         // Actually 4 bits per pixel
        case TextureFormat::RGB_ETC2:         // Actually 4 bits per pixel
        case TextureFormat::RGB_PVRTC1_4BPP:  // Actually 4 bits per pixel
        case TextureFormat::RGBA_PVRTC1_4BPP: // Actually 4 bits per pixel
        case TextureFormat::RGBA_PVRTC2_4BPP: // Actually 4 bits per pixel
            return 1;
        case TextureFormat::R8:
        case TextureFormat::R8I:
        case TextureFormat::R8UI:
        case TextureFormat::RGBA_BC1:
        case TextureFormat::RGBA_BC2:
        case TextureFormat::RGBA_BC3:
        case TextureFormat::RGBA_BC7:
        case TextureFormat::RGBA_ETC2_EAC:
        case TextureFormat::RGBA_ASTC_4x4: return 1;
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
static constexpr size_t format_internal_pixel_byte_size(TextureFormat format)
{
    switch (format)
    {
        case TextureFormat::RGB_BC1:          // Actually 4 bits per pixel
        case TextureFormat::RGB_ETC1:         // Actually 4 bits per pixel
        case TextureFormat::RGB_ETC2:         // Actually 4 bits per pixel
        case TextureFormat::RGB_PVRTC1_4BPP:  // Actually 4 bits per pixel
        case TextureFormat::RGBA_PVRTC1_4BPP: // Actually 4 bits per pixel
        case TextureFormat::RGBA_PVRTC2_4BPP: // Actually 4 bits per pixel
            return 1;
        case TextureFormat::R8:
        case TextureFormat::R8I:
        case TextureFormat::R8UI:
        case TextureFormat::RGBA_BC1:
        case TextureFormat::RGBA_BC2:
        case TextureFormat::RGBA_BC3:
        case TextureFormat::RGBA_BC7:
        case TextureFormat::RGBA_ETC2_EAC:
        case TextureFormat::RGBA_ASTC_4x4: return 1;
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
        case TextureFormat::Depth24Stencil8: return 4;
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

static size_t format_channel_count(TextureFormat format)
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
        case TextureFormat::RGB_PVRTC1_4BPP: return 3;
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
        case TextureFormat::RGBA_ASTC_4x4: return 4;
        default:
        {
            assert(!"Unknown texture format");
            return 0;
        }
    }
}

static const char* format_str(TextureFormat format)
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
        default:
        {
            return "Unknown format";
        }
    }
}

enum class UsageHint
{
    Static,    // Never updated.
    Updatable, // Updated at most once per frame.
    Dynamic,   // Updated many timed per frame.
    Download,  // Download at most once per frame.
};

struct Resource
{
    enum Type
    {
        // Don't use 0 to reserve the null resource handle.
        VertexBuffer = 1,
        IndexBuffer,
        UniformBuffer,
        TextureDownloadBuffer,
        Shader,
        ShaderDerivative,
        VertexInput,
        Texture,
        Renderbuffer,
        Sampler,
        Framebuffer,
    };

    explicit Resource(Type type) : type(type) {}

    Type type;
    bool allow_allocation_failure = false;
};

struct BufferResource : public Resource
{
    enum BufferType
    {
        Vertex,
        Index,
        Uniform,
        TextureDownload,
    };

    static Type to_resource_type(BufferType type)
    {
        switch (type)
        {
            case BufferType::Vertex: return Type::VertexBuffer;
            case BufferType::Index: return Type::IndexBuffer;
            case BufferType::Uniform: return Type::UniformBuffer;
            case BufferType::TextureDownload: return Type::TextureDownloadBuffer;
            default: assert(false && "Unhandled case"); return Type::VertexBuffer;
        }
    }

    explicit BufferResource(BufferType type) : Resource(to_resource_type(type)) {}

    size_t size{};
    const void* data{};
    UsageHint usage{};
};

struct IndexName
{
    int index;
    const char* name;
};

enum class VertexRate
{
    PerVertex = 0,
    PerInstance = 1,
    Per32Instances = 2,
    // On OpenGL, this will use an attrib divisor of 2^31.
    // So be careful with it. :)
    Constant = 3,
};

struct VertexInputStream
{
    int index;
    ResourceHandle buffer;
    VertexFormat format;
    size_t offset;
    size_t stride;
    VertexRate rate;
};

struct VertexInputResource : public Resource
{
    VertexInputResource() : Resource(VertexInput) {}

    uint32_t attrib_count{};
    const VertexInputStream* attribs{};
    my::ResourceHandle indices;
};

struct SamplerParams
{
    enum class Filter
    {
        Nearest,
        Linear,
    };

    enum class Wrap
    {
        Clamp,
        Mirror,
        Repeat,
    };

    Filter min_filter = Filter::Linear;
    Filter mag_filter = Filter::Linear;
    Filter mipmap_filter = Filter::Linear;
    Wrap wrap_x = Wrap::Repeat;
    Wrap wrap_y = Wrap::Repeat;
    Wrap wrap_z = Wrap::Repeat;
    bool is_shadow = false;
    DepthState::Compare compare = DepthState::Compare::Always;
};

struct TextureLayout
{
    enum Type
    {
        Type2D,
        Type3D,
        Array,
    };

    Type type;
    TextureFormat format;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t levels;

    size_t get_level_byte_size(uint32_t level) const;
    size_t get_level_internal_byte_size(uint32_t level) const;

    // For compressed formats, the data that must be supplied
    // for a given level can be made of more pixels than the
    // actual level image. This is because these formats are
    // made out of blocks of multiple pixels.
    uint32_t get_level_data_width(uint32_t level) const;
    uint32_t get_level_data_height(uint32_t level) const;

    uint32_t get_level_width(uint32_t level) const;
    uint32_t get_level_height(uint32_t level) const;
    uint32_t get_level_depth(uint32_t level) const;
};

struct TextureResource : public Resource
{
    TextureResource() : Resource(Texture) {}

    TextureLayout layout;

    // One for each sampler.levels if generate_mipmaps = false.
    // The data for any level can be empty, in which case the
    // level get filled with zeros.
    // The whole data span can be empty if all levels are empty.
    gsl::span<gsl::span<const std::byte>> data;

    bool generate_mipmaps;
    bool is_render_graph_texture = false;
    const char* name = nullptr;
};

struct RenderbufferResource : public Resource
{
    RenderbufferResource() : Resource(Renderbuffer) {}

    TextureFormat format{};
    uint32_t width{};
    uint32_t height{};
    bool is_render_graph_render_buffer = false;
    const char* name = nullptr;
};

struct SamplerResource : public Resource
{
    SamplerResource() : Resource(Sampler) {}

    SamplerParams sampler;
    bool use_mipmaps{};
};

enum class Attachment
{
    Depth,
    Stencil,
    DepthStencil,

    _FirstColor,
    Color0 = _FirstColor,
    Color1,
    Color2,
    Color3,
    Color4,
    Color5,
    _LastColor = Color5,
    _MaxAttachmentPlusOne,
    _ColorCount = _LastColor - _FirstColor + 1
};

struct FramebufferAttachment
{
    Attachment bind_point;
    ResourceHandle texture_or_renderbuffer;
};

struct FramebufferResource : public Resource
{
    FramebufferResource() : Resource(Framebuffer) {}

    uint32_t attachment_count{};
    const FramebufferAttachment* attachments{};
};

// Hints Mycelium on how to behave with regard to shader program linking stage.
enum class ShaderLinkHint
{
    // Schedules shaders to be compiled when first used. Compilation will happen at the rate of one
    // per frame. So use this where it's not too important to have a few blank frames (like when
    // loading already creates frames where the object is not drawn).
    FirstUseDeferred,

    // Sets the shader to be compiled immediately when first used. This avoids having the objects
    // invisible for a few frames, but creates more stuttering.
    FirstUseImmediate,

    // Compile the shader during engine initialization. Only use this for essential shaders.
    Initial,
};

struct ShaderResource : public Resource
{
    ShaderResource() : Resource(Shader) {}

    // The name of shaders is important as shaders with similar names are grouped together:
    // The name is split at its first "_". Everything before is the name of the group.
    // For example "Cylinder_opaque" and "Cylinder_picking" belong to the "Cylinder" group.
    // This is used to compile groups of shaders together when using deferred modes.
    // For example when the "Cylinder_opaque" shader is requested to be compiled, the
    // "Cylinder_picking" shader also will be scheduled for compilation, because it's very probable
    // that it will be used very soon.
    // Of course this only happens when the shaders have not been set to be compiled when idle. So
    // this only applied to "slow" systems where compiling all shaders is too slow.
    const char* name = nullptr;

    ShaderLinkHint link_hint = ShaderLinkHint::FirstUseDeferred;

    size_t vertex_source_len{};
    const char* vertex_source{};
    size_t fragment_source_len{};
    const char* fragment_source{};

    uint32_t attrib_count{};
    const IndexName* attribs{};

    uint32_t uniform_block_count{};
    const IndexName* uniform_blocks{};

    uint32_t sampler_count{};
    const IndexName* samplers{};

    uint32_t output_count{};
    const char* const* outputs{};

    PipelineState initial_state;
};

struct ShaderDerivativeResource : public Resource
{
    ShaderDerivativeResource() : Resource(ShaderDerivative) {}

    explicit ShaderDerivativeResource(
        my::ResourceHandle shader,
        const ShaderResource& res,
        const char* name = nullptr) :
        Resource(ShaderDerivative),
        name(name),
        base_shader(shader),
        initial_state(res.initial_state)
    {
    }

    // See the documentation of ShaderResource.name.
    const char* name;
    my::ResourceHandle base_shader;
    PipelineState initial_state;
};

class Instance;

class ResourceContext
{
public:
    virtual ~ResourceContext() = default;

    virtual ResourceHandle alloc(const Resource*) = 0;
    virtual void dealloc(ResourceHandle) = 0;

    virtual void realloc_buffer(ResourceHandle, const BufferResource*) = 0;
    virtual void update_texture_layout(ResourceHandle, const TextureLayout&) = 0;
    virtual void update_renderbuffer_size(ResourceHandle, uint32_t width, uint32_t height) = 0;

    virtual ResourceHandle retrieve_shader(const char* name) const = 0;
};

struct UboBinding
{
    int index;
    ResourceHandle buffer;
    uint32_t offset;
    uint32_t size;
};

struct TextureBinding
{
    int index;
    ResourceHandle texture;
    ResourceHandle sampler;
};

enum class PrimitiveType
{
    PointList,
    LineList,
    LineStrip,
    LineLoop,
    TriangleList,
    TriangleStrip,
    TriangleFan,
};

enum class CullModifier
{
    DontChange = 0,
    Swap,
    Disable,
};

struct DrawBatchInfo
{
    DrawBatchInfo() = default;

    constexpr DrawBatchInfo(PrimitiveType type_, uint32_t count_) : type(type_), count(count_) {}

    constexpr DrawBatchInfo indexed(IndexType index_type_, uint32_t index_offset_ = 0) &&
    {
        index_type = index_type_;
        index_offset = index_offset_;
        return *this;
    }

    constexpr DrawBatchInfo instanced(uint32_t instances_, uint32_t first_instance_ = 0) &&
    {
        is_instanced = true;
        instances = instances_;
        first_instance = first_instance_;
        return *this;
    }

    constexpr DrawBatchInfo change_cull(CullModifier cull_modifier_) &&
    {
        cull_modifier = cull_modifier_;
        return *this;
    }

    PrimitiveType type;
    uint32_t count;
    IndexType index_type = IndexType::UInt;
    uint32_t index_offset = 0; // Number of elements of "index_type" in the buffer
    bool is_instanced = false;
    uint32_t instances = 1;
    uint32_t first_instance = 0;
    CullModifier cull_modifier = CullModifier::DontChange;
};

struct ClearValue
{
    static ClearValue make_color_float(float r, float g, float b, float a)
    {
        ClearValue v{ColorFloat};
        v.color_float[0] = r;
        v.color_float[1] = g;
        v.color_float[2] = b;
        v.color_float[3] = a;
        return v;
    }

    static ClearValue make_color_uint(uint32_t r, uint32_t g, uint32_t b, uint32_t a)
    {
        ClearValue v{ColorUInt};
        v.color_uint[0] = r;
        v.color_uint[1] = g;
        v.color_uint[2] = b;
        v.color_uint[3] = a;
        return v;
    }

    static ClearValue make_color_int(int32_t r, int32_t g, int32_t b, int32_t a)
    {
        ClearValue v{ColorInt};
        v.color_int[0] = r;
        v.color_int[1] = g;
        v.color_int[2] = b;
        v.color_int[3] = a;
        return v;
    }

    static ClearValue make_depth(float depth)
    {
        ClearValue v{Depth};
        v.depth = depth;
        return v;
    }

    static ClearValue make_stencil(int32_t stencil)
    {
        ClearValue v{Stencil};
        v.stencil = stencil;
        return v;
    }

    static ClearValue make_depth_stencil(float depth, int32_t stencil)
    {
        ClearValue v{DepthStencil};
        v.ds.depth = depth;
        v.ds.stencil = stencil;
        return v;
    }

    enum Type
    {
        ColorFloat,
        ColorInt,
        ColorUInt,
        Depth,
        Stencil,
        DepthStencil,
    };

    Type type;

    union
    {
        float color_float[4];
        int32_t color_int[4];
        uint32_t color_uint[4];
        float depth;
        int32_t stencil;

        struct
        {
            float depth;
            int32_t stencil;
        } ds;
    };
};

struct ClearTarget
{
    Attachment attachment;
    ClearValue value;
};

enum AspectFlags
{
    Aspect_None = 0,
    Aspect_Color = 1,
    Aspect_Depth = 2,
    Aspect_Stencil = 4,
    Aspect_DepthStencil = Aspect_Depth | Aspect_Stencil,
    Aspect_All = Aspect_Color | Aspect_Depth | Aspect_Stencil,
};

enum class TextureDownloadFormat
{
    RGBA8Norm,
    RGBA32F,
    RGBA32I,
    RGBA32UI,
};

struct TextureDownloadData
{
    TextureFormat format{};
    std::unique_ptr<char[]> data;
};

enum class QueryTimeStatus
{
    OK,
    NOT_AVAILABLE,
    NOT_FOUND,
    INVALID
};

struct QueryTimeResult
{
    std::string name;
    float elapsed_time;
};

struct ResourceAllocationReport
{
    enum class Type
    {
        Allocation,
        Reallocation,
        Deallocation,
    };

    Type type;
    ResourceHandle resource_handle;
    Resource::Type resource_type;
    size_t resource_size;
};

class RenderContext
{
public:
    virtual ~RenderContext() = default;

    virtual void clear(uint32_t clear_count, const ClearTarget* values) = 0;

    virtual void set_viewport(const ViewportState&) = 0;
    virtual void set_framebuffer(ResourceHandle fbo, const ViewportState&) = 0;

    virtual void update_buffer(
        ResourceHandle buffer,
        size_t offset,
        size_t size,
        const void* data) = 0;

    enum TextureUpdateDataLayout
    {
        DataHasUpdateRegionSize = 0,
        DataHasTargetTextureSize = 1,
    };

    virtual void update_texture(
        ResourceHandle texture,
        TextureFormat format,
        int level,
        uint32_t x,
        uint32_t y,
        uint32_t z,
        uint32_t w,
        uint32_t h,
        uint32_t d,
        gsl::span<const std::byte> data,
        TextureUpdateDataLayout data_layout = TextureUpdateDataLayout::DataHasUpdateRegionSize) = 0;

    virtual void draw(
        const DrawBatchInfo& info,
        ResourceHandle shader,
        ResourceHandle vertex_input,
        uint32_t ubo_count,
        const UboBinding* ubos,
        uint32_t texture_count,
        const TextureBinding* textures) = 0;

    virtual void blit_framebuffers(
        ResourceHandle src,
        Rect src_rect,
        Rect dst_rect,
        AspectFlags,
        Attachment src_attachment,
        uint32_t dst_attachment_count,
        const Attachment* dst_attachments,
        SamplerParams::Filter) = 0;

    void blit_framebuffers_depth(
        ResourceHandle src,
        Rect src_rect,
        Rect dst_rect,
        SamplerParams::Filter filter)
    {
        blit_framebuffers(
            src, src_rect, dst_rect, Aspect_Depth, Attachment::Depth, 0, nullptr, filter);
    }

    void blit_framebuffers_stencil(
        ResourceHandle src,
        Rect src_rect,
        Rect dst_rect,
        SamplerParams::Filter filter)
    {
        blit_framebuffers(
            src, src_rect, dst_rect, Aspect_Stencil, Attachment::Stencil, 0, nullptr, filter);
    }

    void blit_framebuffers_depth_stencil(
        ResourceHandle src,
        Rect src_rect,
        Rect dst_rect,
        SamplerParams::Filter filter)
    {
        blit_framebuffers(
            src, src_rect, dst_rect, Aspect_DepthStencil, Attachment::DepthStencil, 0, nullptr,
            filter);
    }

    void blit_framebuffers_colors(
        ResourceHandle src,
        Rect src_rect,
        Rect dst_rect,
        Attachment src_attachment,
        uint32_t dst_attachment_count,
        const Attachment* dst_attachments,
        SamplerParams::Filter filter)
    {
        blit_framebuffers(
            src, src_rect, dst_rect, Aspect_Color, src_attachment, dst_attachment_count,
            dst_attachments, filter);
    }

    virtual TextureDownloadData color_texture_download_sync(
        ResourceHandle framebuffer,
        Attachment color_attachment,
        Rect rect,
        TextureDownloadFormat format) = 0;

    virtual void color_texture_download_async(
        uint64_t download_id,
        ResourceHandle framebuffer,
        Attachment color_attachment,
        Rect rect,
        TextureDownloadFormat format,
        ResourceHandle buffer) = 0;

    virtual void begin_time_query(uint64_t query_id, const char* name) = 0;

    virtual void end_time_query(uint64_t query_id) = 0;
};

class Instance : public ResourceContext, public RenderContext
{
public:
    struct Info
    {
        const char* vendor;
        const char* renderer;
        const char* version;
        bool has_disjoint_time_query;
        bool has_gpu_memory_info;
        bool has_parallel_shader_compile;
        bool has_texture_float_linear;
        bool has_bc1_bc2_bc3_texture_compression;
        bool has_bc7_texture_compression;
        bool has_etc1_texture_compression;
        bool has_etc2_texture_compression;
        bool has_astc_texture_compression;
        bool has_pvrtc_texture_compression;
        bool has_pvrtc2_texture_compression;
    };

    struct GpuMemoryInfo
    {
        // Values are in bytes
        uint64_t total_device_memory;
        uint64_t usable_memory;
        uint64_t free_memory;
    };

    struct ShadersInfo
    {
        int to_link_initial{};
        int to_link_initial_done{};

        int root_shaders{};
        int root_shaders_linked{};
        int shader_derivatives{};

        bool had_unlinked_shaders_last_frame{};
        bool had_unlinked_shaders_this_frame{};
    };

    struct ShadersLinkingConfig
    {
        // When set to true, is the system is able to compile shaders in parallel. Big batches of
        // shaders will be scheduled for compilation at once instead of one per frame.
        // Set this to false on systems where parallel compilation is badly implemented.
        bool allow_parallel_shader_compile{};

        // Set this to true to schedule all shaders to be compiled on idle frames.
        // Set this to false on systems where shader compilation is slow enough to degrade the user
        // experience.
        bool allow_compile_all_when_idle{};

        // Set all shaders to be compiled at initialization. This avoids most stutters after
        // initialization, but makes initialization slower.
        bool force_compile_all_initial{};
    };

    using LoadFn = void* (*)(const char*);

    // The load function is only necessary for EGL.
    static bool init(LoadFn fn = nullptr);

    static Instance* create();

    ~Instance() override = default;

    virtual const Info& get_info() const = 0;

    virtual size_t get_uniform_buffer_offset_alignment() const = 0;
    virtual bool is_texture_format_available(TextureFormat) const = 0;

    virtual void configure_shaders_linking(const ShadersLinkingConfig&) = 0;
    virtual void advance_shaders_link(bool idle = false) = 0;
    virtual ResourceHandle retrieve_shader(const char* name) const override = 0;
    virtual void add_global_shader_define(const char* name, const char* value = "") = 0;

    virtual bool is_texture_download_ready(uint64_t id) const = 0;
    virtual void cancel_texture_download(uint64_t id) = 0;
    virtual TextureDownloadData retrieve_texture_download(uint64_t id) = 0;

    virtual QueryTimeResult retrieve_elapsed_time(uint64_t id, QueryTimeStatus& status) = 0;
    virtual void delete_query(uint64_t id) = 0;

    virtual void activate_resource_allocation_reports() = 0;
    virtual void deactivate_resource_allocation_reports() = 0;
    virtual size_t get_resource_allocation_report_count() const = 0;
    virtual const ResourceAllocationReport* get_resource_allocation_reports() = 0;
    virtual void clear_resource_allocation_reports() = 0;

    virtual void set_memory_limit(uint64_t size_bytes) = 0;
    virtual GpuMemoryInfo get_memory_info() = 0;
    virtual ShadersInfo get_shaders_info() const = 0;

    virtual bool begin_frame() = 0;
    virtual void end_frame() = 0;
};

} // namespace my

template<>
struct std::hash<my::ResourceHandle>
{
    std::size_t operator()(const my::ResourceHandle& s) const noexcept
    {
        return std::hash<uint64_t>{}(s.handle);
    }
};
