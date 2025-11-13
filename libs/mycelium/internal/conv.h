#pragma once

#if MYCELIUM_WEBGL_2
#    include <GLES3/gl3.h>
#elif MYCELIUM_USE_GLAD
#    include <glad/glad.h>
#endif

#include "mycelium/mycelium.h"

// Not all texture compression formats are available on every GL API
// (OpenGL, OpenGL ES, WebGL). Because of this, all constants are not
// defined in glad.h or gl3.h (provided by Emscripten).
// Selectively undefining out some lines depending on the platform
// would be tedious, and would increase platform differences in builds.
// The values of the constants are, well, constant among all APIs.
// They can be seen here:
//     https://raw.githubusercontent.com/KhronosGroup/OpenGL-Registry/master/xml/gl.xml
// The most convenient thing to do is to define here the missing cons-
// tants. We must check whether a compressed format is available before
// using it in any case.
#ifndef GL_COMPRESSED_RGB_S3TC_DXT1_EXT
#    define GL_COMPRESSED_RGB_S3TC_DXT1_EXT 0x83F0
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#    define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
#    define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#    define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif
#ifndef GL_COMPRESSED_RGBA_BPTC_UNORM_ARB
#    define GL_COMPRESSED_RGBA_BPTC_UNORM_ARB 0x8E8C
#endif
#ifndef GL_ETC1_RGB8_OES
#    define GL_ETC1_RGB8_OES 0x8D64
#endif
#ifndef GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG
#    define GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG 0x8C00
#endif
#ifndef GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG
#    define GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG 0x8C02
#endif
#ifndef GL_COMPRESSED_RGBA_PVRTC_4BPPV2_IMG
#    define GL_COMPRESSED_RGBA_PVRTC_4BPPV2_IMG 0x9138
#endif
#ifndef GL_COMPRESSED_RGBA_ASTC_4x4_KHR
#    define GL_COMPRESSED_RGBA_ASTC_4x4_KHR 0x93B0
#endif
#ifndef GL_COMPRESSED_SRGB_S3TC_DXT1_EXT
#    define GL_COMPRESSED_SRGB_S3TC_DXT1_EXT 0x8C4C
#endif
#ifndef GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT
#    define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT 0x8C4D
#endif
#ifndef GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT
#    define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT 0x8C4E
#endif
#ifndef GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT
#    define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT 0x8C4F
#endif
#ifndef GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM_ARB
#    define GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM_ARB 0x8E8D
#endif
#ifndef GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR
#    define GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR 0x93D0
#endif
#ifndef GL_COMPRESSED_SRGB_PVRTC_4BPPV1_EXT
#    define GL_COMPRESSED_SRGB_PVRTC_4BPPV1_EXT 0x8A55
#endif
#ifndef GL_COMPRESSED_SRGB_ALPHA_PVRTC_4BPPV1_EXT
#    define GL_COMPRESSED_SRGB_ALPHA_PVRTC_4BPPV1_EXT 0x8A57
#endif
#ifndef GL_COMPRESSED_SRGB_ALPHA_PVRTC_4BPPV2_IMG
#    define GL_COMPRESSED_SRGB_ALPHA_PVRTC_4BPPV2_IMG 0x93F1
#endif

namespace my
{

GLenum to_gl(PrimitiveType p);

GLint format_components(VertexFormat format);
GLenum format_type(VertexFormat format);
bool format_normalized(VertexFormat format);
bool format_integer(VertexFormat format);

size_t format_format(TextureFormat format);
size_t format_type(TextureFormat format);
size_t format_internal(TextureFormat format);

GLenum to_gl(IndexType type);
GLenum to_gl(UsageHint usage);
GLenum to_gl(TextureLayout::Type type);
GLenum to_gl(SamplerParams::Wrap wrap);

GLenum to_gl(
    SamplerParams::Filter filter,
    SamplerParams::Filter mip_filter = SamplerParams::Filter::Linear,
    bool use_mipmaps = false);

GLenum to_gl(my::RasterizationState::CullMode mode);
GLenum to_gl(my::RasterizationState::FrontFace face);
GLenum to_gl(my::DepthState::Compare func);
GLenum to_gl(my::StencilState::Compare func);
GLenum to_gl(my::StencilState::Op op);

inline GLenum to_gl(bool b)
{
    return b ? GL_TRUE : GL_FALSE;
}

GLenum to_gl(ColorBlendState::Op op);
GLenum to_gl(ColorBlendState::Factor factor);
GLenum to_gl(Attachment a);
GLbitfield to_gl(AspectFlags aspects);

GLenum buffer_resource_type_to_gl_target(Resource::Type type);

TextureFormat to_texture_format(TextureDownloadFormat format);
GLenum dl_format_format(TextureDownloadFormat format);
GLenum dl_format_type(TextureDownloadFormat format);

} // namespace my
