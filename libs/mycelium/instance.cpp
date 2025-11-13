#include "internal/instance.h"

#include "internal/conv.h"
#include "internal/log.h"

#include <string.h>

#include <stdio.h>
#include <stdlib.h>

#define my_MAX(x, y) (((x) > (y)) ? (x) : (y))

#ifndef MYCELIUM_DEBUG_SHADERS_LINK_TIME
#    define MYCELIUM_DEBUG_SHADERS_LINK_TIME 0
#endif

#if MYCELIUM_DEBUG_SHADERS_LINK_TIME
using Clock = std::chrono::high_resolution_clock;
static Clock::time_point epoch = Clock::now();

static inline double now_ms()
{
    using Duration = std::chrono::duration<double, std::milli>;
    Duration duration = std::chrono::duration_cast<Duration>(Clock::now() - epoch);
    return duration.count();
}
#endif // MYCELIUM_DEBUG_SHADERS_LINK_TIME

#if MYCELIUM_WEBGL_2

#    include "mycelium/js/lib.h"

#    include <emscripten/emscripten.h>

// This is in WebGL2 but not in OpenGL ES 3.0 so Emscripten doesn't provide
// it because apparently providing bindings for web APIs is not its job...
static void myGetBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, GLvoid* data)
{
    EM_ASM({ GLctx.getBufferSubData($0, $1, HEAPU8, $2, $3); }, target, offset, data, size);
}

// Also Emscripten fails to link this...
static void myPolygonOffset(GLfloat factor, GLfloat units)
{
    EM_ASM({ GLctx.polygonOffset($0, $1); }, factor, units);
}

// clang-format off
#define HAS_WEBGL_EXTENSION(ext) \
    (EM_ASM_INT({ return GLctx.getExtension(ext) != null; }) != 0)
// clang-format on

#    define GL_UNMASKED_VENDOR_WEBGL 0x9245
#    define GL_UNMASKED_RENDERER_WEBGL 0x9246

#    define GL_FIRST_VERTEX_CONVENTION 0x8E4D

// The spec says:
//     Implementations SHOULD only expose this extension when FIRST_VERTEX_CONVENTION
//     is more efficient than the default behavior of LAST_VERTEX_CONVENTION.
//     Applications should expect that if this extension is supported by a WebGL context,
//     they should try to use FIRST_VERTEX_CONVENTION if they can.
// See https://registry.khronos.org/webgl/extensions/WEBGL_provoking_vertex/
//
// Graphics APIs other than OpenGL use the attribute values of the first vertex when
// drawing with flat shading. OpenGL uses the last vertex.
// Not all of the other APIs have ways to change that convention, so WebGL implemen-
// tations may have to do complex stuff to emulate the last vertex convention, such
// as adding an intermediate geometry shader to invert the triangles' winding order.
// Using the native convention is faster, and avoids bugs in the translation layer
// of the WebGL implementations, for example on iOS.
static void set_preferred_provoking_vertex()
{
    EM_ASM(
        {
            var extension = GLctx.getExtension("WEBGL_provoking_vertex");
            if (extension)
            {
                extension.provokingVertexWEBGL($0);
            }
        },
        GL_FIRST_VERTEX_CONVENTION);
}
#endif

namespace my
{
bool operator!=(const Rect& a, const Rect& b)
{
    return a.x != b.x || a.y != b.y || a.w != b.w || a.h != b.h;
}

bool operator!=(const Color& a, const Color& b)
{
    return a.r != b.r || a.g != b.g || a.b != b.b || a.a != b.a;
}

bool operator!=(const ViewportState& a, const ViewportState& b)
{
    return a.viewport != b.viewport || a.scissor != b.scissor;
}

bool operator!=(const RasterizationState& a, const RasterizationState& b)
{
    return a.depth_bias_factor != b.depth_bias_factor || a.depth_bias_units != b.depth_bias_units
        || a.cull_mode != b.cull_mode || a.front_face != b.front_face
        || a.line_width != b.line_width;
}

bool operator!=(const DepthState& a, const DepthState& b)
{
    return a.test != b.test || a.write != b.write || a.compare != b.compare;
}

bool operator!=(const StencilState::Face& a, const StencilState::Face& b)
{
    return a.fail_op != b.fail_op || a.depth_pass_op != b.depth_pass_op
        || a.depth_fail_op != b.depth_fail_op || a.compare != b.compare
        || a.compare_mask != b.compare_mask || a.write_mask != b.write_mask
        || a.reference != b.reference;
}

bool operator!=(const StencilState& a, const StencilState& b)
{
    return a.enable != b.enable || a.front != b.front || a.back != b.back;
}

bool operator!=(const ColorBlendState::Channel& a, const ColorBlendState::Channel& b)
{
    return a.src != b.src || a.dst != b.dst || a.op != b.op;
}

bool operator!=(const ColorBlendState& a, const ColorBlendState& b)
{
    return a.enable != b.enable || a.constant_color != b.constant_color || a.mask != b.mask
        || a.color != b.color || a.alpha != b.alpha;
}

} // namespace my

bool my::Instance::init(LoadFn load_fn)
{
#if MYCELIUM_USE_GLAD
#    if MYCELIUM_GL_DESKTOP
    if (gladLoadGL() == 0) return false;
#    elif MYCELIUM_GL_ES
    if (gladLoadGLES2Loader(load_fn) == 0) return false;
#    endif
#endif

    glEnable(GL_SCISSOR_TEST);
    GL_ERROR();
#if MYCELIUM_GL_DESKTOP
    glEnable(GL_PROGRAM_POINT_SIZE);
    GL_ERROR();
#endif

#ifdef MYCELIUM_WEBGL_2
    set_preferred_provoking_vertex();
#endif

    return true;
}

my::Instance* my::Instance::create()
{
    return new GLInstance();
}

const char* my_glGetStringCopy(GLenum value)
{
    auto* gl_str = (const char*)glGetString(value);
    if (!gl_str) return nullptr;

    auto* new_str = new char[strlen(gl_str) + 1];
    strcpy(new_str, gl_str);
    return new_str;
}

my::GLInstance::GLInstance() : Instance()
{
    for (int i = 0; i < MaxTextureUnits; ++i)
    {
        _last_textures[i].target = 0;
        _last_textures[i].texture = ~0u;
        _last_textures[i].sampler = 0;
    }

    _current_state = PipelineState{};

    _instance_info.vendor = my_glGetStringCopy(GL_VENDOR);
    _instance_info.renderer = my_glGetStringCopy(GL_RENDERER);
    _instance_info.version = my_glGetStringCopy(GL_VERSION);

#ifdef MYCELIUM_WEBGL_2
    if (HAS_WEBGL_EXTENSION("WEBGL_debug_renderer_info"))
    {
        _instance_info.renderer = my_glGetStringCopy(GL_UNMASKED_RENDERER_WEBGL);
        _instance_info.vendor = my_glGetStringCopy(GL_UNMASKED_VENDOR_WEBGL);
    }

    _instance_info.has_disjoint_time_query = HAS_WEBGL_EXTENSION("EXT_disjoint_timer_query_webgl2");
    _instance_info.has_parallel_shader_compile = HAS_WEBGL_EXTENSION("KHR_parallel_shader_compile");
    _instance_info.has_texture_float_linear = HAS_WEBGL_EXTENSION("OES_texture_float_linear");
#elif MYCELIUM_GL_ES
    _instance_info.has_disjoint_time_query = GLAD_GL_EXT_disjoint_timer_query;
    _instance_info.has_parallel_shader_compile = false;
    _instance_info.has_texture_float_linear = true;
#else
    _instance_info.has_disjoint_time_query = true;
    _instance_info.has_parallel_shader_compile = false;
    _instance_info.has_texture_float_linear = true;
#endif

#ifdef MYCELIUM_CORE_33
    _instance_info.has_bc1_bc2_bc3_texture_compression = GLAD_GL_EXT_texture_compression_s3tc;
    _instance_info.has_bc1_bc2_bc3_srgb_texture_compression =
        GLAD_GL_EXT_texture_compression_s3tc && GLAD_GL_ARB_texture_view;
    _instance_info.has_bc7_texture_compression = GLAD_GL_ARB_texture_compression_bptc;
    _instance_info.has_bc7_srgb_texture_compression = GLAD_GL_ARB_texture_compression_bptc;
    _instance_info.has_etc1_texture_compression = false;
    _instance_info.has_etc1_srgb_texture_compression = false;
    _instance_info.has_etc2_texture_compression = GLAD_GL_ARB_ES3_compatibility;
    _instance_info.has_etc2_srgb_texture_compression = GLAD_GL_ARB_ES3_compatibility;
    _instance_info.has_astc_texture_compression = GLAD_GL_KHR_texture_compression_astc_ldr;
    _instance_info.has_astc_srgb_texture_compression = GLAD_GL_KHR_texture_compression_astc_ldr;
    _instance_info.has_pvrtc_texture_compression = false;
    _instance_info.has_pvrtc_srgb_texture_compression = false;
    _instance_info.has_pvrtc2_texture_compression = false;
    _instance_info.has_pvrtc2_srgb_texture_compression = false;
#elif MYCELIUM_ES_30
    _instance_info.has_bc1_bc2_bc3_texture_compression = GLAD_GL_EXT_texture_compression_s3tc;
    _instance_info.has_bc1_bc2_bc3_srgb_texture_compression =
        GLAD_GL_EXT_texture_compression_s3tc && GLAD_GL_ARB_texture_view;
    _instance_info.has_bc7_texture_compression = GLAD_GL_EXT_texture_compression_bptc;
    _instance_info.has_bc7_srgb_texture_compression = GLAD_GL_EXT_texture_compression_bptc;
    _instance_info.has_etc1_texture_compression = GLAD_GL_OES_compressed_ETC1_RGB8_texture;
    _instance_info.has_etc1_srgb_texture_compression = true;
    _instance_info.has_etc2_texture_compression = true;
    _instance_info.has_etc2_srgb_texture_compression = true;
    _instance_info.has_astc_texture_compression = GLAD_GL_KHR_texture_compression_astc_ldr;
    _instance_info.has_astc_srgb_texture_compression = GLAD_GL_KHR_texture_compression_astc_ldr;
    _instance_info.has_pvrtc_texture_compression = GLAD_GL_IMG_texture_compression_pvrtc;
    _instance_info.has_pvrtc_srgb_texture_compression =
        GLAD_GL_IMG_texture_compression_pvrtc && GLAD_GL_EXT_pvrtc_sRGB;
    _instance_info.has_pvrtc2_texture_compression = GLAD_GL_IMG_texture_compression_pvrtc2;
    _instance_info.has_pvrtc2_srgb_texture_compression =
        GLAD_GL_IMG_texture_compression_pvrtc2 && GLAD_GL_EXT_pvrtc_sRGB;
#elif MYCELIUM_WEBGL_2
    _instance_info.has_bc1_bc2_bc3_texture_compression =
        HAS_WEBGL_EXTENSION("WEBGL_compressed_texture_s3tc");
    _instance_info.has_bc1_bc2_bc3_srgb_texture_compression =
        HAS_WEBGL_EXTENSION("WEBGL_compressed_texture_s3tc_srgb");
    _instance_info.has_bc7_texture_compression =
        HAS_WEBGL_EXTENSION("EXT_texture_compression_bptc");
    _instance_info.has_bc7_srgb_texture_compression =
        HAS_WEBGL_EXTENSION("EXT_texture_compression_bptc");
    _instance_info.has_etc1_texture_compression =
        HAS_WEBGL_EXTENSION("WEBGL_compressed_texture_etc1");
    _instance_info.has_etc1_srgb_texture_compression = false;
    _instance_info.has_etc2_texture_compression =
        HAS_WEBGL_EXTENSION("WEBGL_compressed_texture_etc");
    _instance_info.has_etc2_srgb_texture_compression =
        HAS_WEBGL_EXTENSION("WEBGL_compressed_texture_etc");
    _instance_info.has_astc_texture_compression =
        HAS_WEBGL_EXTENSION("WEBGL_compressed_texture_astc");
    _instance_info.has_astc_srgb_texture_compression =
        HAS_WEBGL_EXTENSION("WEBGL_compressed_texture_astc");
    _instance_info.has_pvrtc_texture_compression =
        HAS_WEBGL_EXTENSION("WEBGL_compressed_texture_pvrtc")
        || HAS_WEBGL_EXTENSION("WEBKIT_WEBGL_compressed_texture_pvrtc");
    _instance_info.has_pvrtc_srgb_texture_compression = false;
    _instance_info.has_pvrtc2_texture_compression = false;
    _instance_info.has_pvrtc2_srgb_texture_compression = false;
#endif

    _gpu_memory_info_type = GpuMemoryInfoType::None;
    _instance_info.has_gpu_memory_info = false;
#ifdef MYCELIUM_CORE_33
    if (GLAD_GL_NVX_gpu_memory_info)
    {
        _gpu_memory_info_type = GpuMemoryInfoType::NVX_gpu_memory_info;
        _instance_info.has_gpu_memory_info = true;
    }
#endif

    int value;
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &value);
    GL_ERROR();
    _uniform_buffer_offset_alignment = value;

#ifndef MYCELIUM_WEBGL_2
    glEnable(GL_FRAMEBUFFER_SRGB);
#endif

    _store_resource_size_reports = false;

    _shaders_info = ShadersInfo{};
}

void my::GLInstance::add_global_shader_define(const char* name, const char* value)
{
    _global_shader_defines += "#define ";
    _global_shader_defines += name;
    _global_shader_defines += " ";
    _global_shader_defines += value;
    _global_shader_defines += "\n";
}

size_t my::GLInstance::get_uniform_buffer_offset_alignment() const
{
    return _uniform_buffer_offset_alignment;
}

bool my::GLInstance::is_texture_format_available(TextureFormat format) const
{
    if (is_format_compressed(format))
    {
        switch (format)
        {
            case TextureFormat::RGB_BC1:
            case TextureFormat::RGBA_BC1:
            case TextureFormat::RGBA_BC2:
            case TextureFormat::RGBA_BC3: return _instance_info.has_bc1_bc2_bc3_texture_compression;
            case TextureFormat::RGBA_BC7: return _instance_info.has_bc7_texture_compression;
            case TextureFormat::RGB_ETC1: return _instance_info.has_etc1_texture_compression;
            case TextureFormat::RGB_ETC2:
            case TextureFormat::RGBA_ETC2_EAC: return _instance_info.has_etc2_texture_compression;
            case TextureFormat::RGB_PVRTC1_4BPP:
            case TextureFormat::RGBA_PVRTC1_4BPP:
                return _instance_info.has_pvrtc_texture_compression;
            case TextureFormat::RGBA_PVRTC2_4BPP:
                return _instance_info.has_pvrtc2_texture_compression;
            case TextureFormat::RGBA_ASTC_4x4: return _instance_info.has_astc_texture_compression;
            default: assert(false && "Unhandled case"); return false;
        }
    }
    else
    {
        return true;
    }
}

static my::RasterizationState apply_cull_modifier(
    my::RasterizationState state,
    my::CullModifier modifier)
{
    using namespace my;
    using CullMode = RasterizationState::CullMode;
    using FrontFace = RasterizationState::FrontFace;

    switch (modifier)
    {
        case CullModifier::DontChange: break;
        case CullModifier::Swap:
        {
            switch (state.front_face)
            {
                case FrontFace::Clockwise:
                {
                    state.front_face = FrontFace::CounterClockwise;
                    break;
                }
                case FrontFace::CounterClockwise:
                {
                    state.front_face = FrontFace::Clockwise;
                    break;
                }
            }
            break;
        }
        case CullModifier::Disable:
        {
            state.cull_mode = CullMode::None;
            break;
        }
    }

    return state;
}

void my::GLInstance::apply_pipeline_state(PipelineState state, CullModifier cull_modifier)
{
    state.rasterization = apply_cull_modifier(state.rasterization, cull_modifier);

    if (state.rasterization != _current_state.rasterization)
    {
        apply_rasterization_state(state.rasterization);
    }

    if (state.depth != _current_state.depth)
    {
        apply_depth_state(state.depth);
    }

    if (state.stencil != _current_state.stencil)
    {
        apply_stencil_state(state.stencil);
    }

    if (state.color_blend != _current_state.color_blend)
    {
        apply_color_blend_state(state.color_blend);
    }
}

void my::GLInstance::apply_viewport_state(const ViewportState& state)
{
    if (state != _current_viewport)
    {
        glViewport(state.viewport.x, state.viewport.y, state.viewport.w, state.viewport.h);

        glScissor(state.scissor.x, state.scissor.y, state.scissor.w, state.scissor.h);
        GL_ERROR();

        _current_viewport = state;
    }
}

void my::GLInstance::apply_rasterization_state(const RasterizationState& state)
{
    const auto& current_state = _current_state.rasterization;

    if (state.cull_mode != current_state.cull_mode)
    {
        if (state.cull_mode == RasterizationState::None)
        {
            glDisable(GL_CULL_FACE);
        }
        else
        {
            glEnable(GL_CULL_FACE);
            glCullFace(to_gl(state.cull_mode));
        }
        GL_ERROR();
    }

    if (state.front_face != current_state.front_face)
    {
        glFrontFace(to_gl(state.front_face));
        GL_ERROR();
    }

    if (state.depth_bias_factor != current_state.depth_bias_factor
        || state.depth_bias_units != current_state.depth_bias_units)
    {
        if (state.depth_bias_factor != 0.0f || state.depth_bias_units != 0.0f)
        {
            glEnable(GL_POLYGON_OFFSET_FILL);
#if MYCELIUM_WEBGL_2
            myPolygonOffset(state.depth_bias_factor, state.depth_bias_units);
#else
            glPolygonOffset(state.depth_bias_factor, state.depth_bias_units);
#endif
            GL_ERROR();
        }
        else
        {
            glDisable(GL_POLYGON_OFFSET_FILL);
#if MYCELIUM_GL_DESKTOP
            glDisable(GL_POLYGON_OFFSET_LINE);
            glDisable(GL_POLYGON_OFFSET_POINT);
#endif
            GL_ERROR();
        }
    }

    if (state.line_width != current_state.line_width)
    {
        glLineWidth(std::max(state.line_width, 1.0f));
        GL_ERROR();
    }

    _current_state.rasterization = state;
}

void my::GLInstance::apply_depth_state(const DepthState& state)
{
    const auto& current_state = _current_state.depth;

    if (state.test != current_state.test)
    {
        if (state.test)
        {
            glEnable(GL_DEPTH_TEST);
        }
        else
        {
            glDisable(GL_DEPTH_TEST);
        }
        GL_ERROR();
    }

    if (state.write != current_state.write)
    {
        glDepthMask(to_gl(state.write));
        GL_ERROR();
    }

    if (state.compare != current_state.compare)
    {
        glDepthFunc(to_gl(state.compare));
        GL_ERROR();
    }

    _current_state.depth = state;
}

void my::GLInstance::apply_stencil_state(const StencilState& state)
{
    auto& current_state = _current_state.stencil;

    if (state.enable != current_state.enable)
    {
        if (state.enable)
        {
            glEnable(GL_STENCIL_TEST);
        }
        else
        {
            glDisable(GL_STENCIL_TEST);
        }
        GL_ERROR();

        current_state.enable = state.enable;
    }

    if (state.enable)
    {
        if (state.front != current_state.front)
        {
            apply_stencil_state_face(GL_FRONT, state.front);
            current_state.front = state.front;
        }

        if (state.back != current_state.back)
        {
            apply_stencil_state_face(GL_BACK, state.back);
            current_state.back = state.back;
        }
    }
}

void my::GLInstance::apply_stencil_state_face(GLenum face, const StencilState::Face& state) const
{
    glStencilOpSeparate(
        face, to_gl(state.fail_op), to_gl(state.depth_fail_op), to_gl(state.depth_pass_op));

    glStencilFuncSeparate(face, to_gl(state.compare), state.reference, state.compare_mask);

    glStencilMaskSeparate(face, state.write_mask);
    GL_ERROR();
}

void my::GLInstance::apply_color_blend_state(const ColorBlendState& state)
{
    const auto& current_state = _current_state.color_blend;

    if (state.enable != current_state.enable)
    {
        if (state.enable)
        {
            glEnable(GL_BLEND);
        }
        else
        {
            glDisable(GL_BLEND);
        }
        GL_ERROR();
    }

    if (state.constant_color != current_state.constant_color)
    {
        glBlendColor(
            state.constant_color.r, state.constant_color.g, state.constant_color.b,
            state.constant_color.a);
        GL_ERROR();
    }

    if (state.color != current_state.color || state.alpha != current_state.alpha)
    {
        glBlendFuncSeparate(
            to_gl(state.color.src), to_gl(state.color.dst), to_gl(state.alpha.src),
            to_gl(state.alpha.dst));

        glBlendEquationSeparate(to_gl(state.color.op), to_gl(state.alpha.op));
        GL_ERROR();
    }

    if (state.mask != current_state.mask)
    {
        int mask = (int)state.mask;
        glColorMask(
            to_gl((mask & ColorBlendState::R) != 0), to_gl((mask & ColorBlendState::G) != 0),
            to_gl((mask & ColorBlendState::B) != 0), to_gl((mask & ColorBlendState::A) != 0));
        GL_ERROR();
    }

    _current_state.color_blend = state;
}

uint32_t my::TextureLayout::get_level_data_width(uint32_t level) const
{
    return my_MAX(width >> level, is_format_compressed(format) ? 4 : 1);
}

uint32_t my::TextureLayout::get_level_data_height(uint32_t level) const
{
    return my_MAX(height >> level, is_format_compressed(format) ? 4 : 1);
}

uint32_t my::TextureLayout::get_level_width(uint32_t level) const
{
    return my_MAX(width >> level, 1);
}

uint32_t my::TextureLayout::get_level_height(uint32_t level) const
{
    return my_MAX(height >> level, 1);
}

uint32_t my::TextureLayout::get_level_depth(uint32_t level) const
{
    switch (type)
    {
        case Type3D: return my_MAX(depth >> level, 1);
        case Type2D: return 1;
        case Array: return my_MAX(depth, 1);
        default: assert(!"Unhandled texture type"); return 0;
    }
}

size_t my::TextureLayout::get_level_byte_size(uint32_t level) const
{
    size_t level_width = get_level_data_width(level);
    size_t level_height = get_level_data_height(level);
    size_t level_depth = get_level_depth(level);

    if (is_format_compressed(format))
    {
        size_t block_byte_size = 0;

        switch (format)
        {
            case TextureFormat::RGB_BC1:
            case TextureFormat::RGBA_BC1:
            case TextureFormat::RGB_ETC1:
            case TextureFormat::RGB_ETC2:
            case TextureFormat::RGBA_PVRTC2_4BPP:
            case TextureFormat::SRGB_BC1:
            case TextureFormat::SRGBA_BC1:
            case TextureFormat::SRGB_ETC1:
            case TextureFormat::SRGB_ETC2:
            case TextureFormat::SRGBA_PVRTC2_4BPP: block_byte_size = 8; break;
            case TextureFormat::RGBA_BC2:
            case TextureFormat::RGBA_BC3:
            case TextureFormat::RGBA_BC7:
            case TextureFormat::RGBA_ETC2_EAC:
            case TextureFormat::RGBA_ASTC_4x4:
            case TextureFormat::SRGBA_BC2:
            case TextureFormat::SRGBA_BC3:
            case TextureFormat::SRGBA_BC7:
            case TextureFormat::SRGBA_ETC2_EAC:
            case TextureFormat::SRGBA_ASTC_4x4: block_byte_size = 16; break;
            case TextureFormat::RGB_PVRTC1_4BPP:
            case TextureFormat::RGBA_PVRTC1_4BPP:
            case TextureFormat::SRGB_PVRTC1_4BPP:
            case TextureFormat::SRGBA_PVRTC1_4BPP:
                return my_MAX(level_width, 8) * my_MAX(level_height, 8) / 2;
            default: assert(false && "Unhandled case"); break;
        }

        return std::floor((level_width + 3) / 4) * std::floor((level_height + 3) / 4)
            * std::floor((level_depth + 3) / 4) * block_byte_size;
    }
    else
    {
        return level_width * level_height * level_depth * format_external_pixel_byte_size(format);
    }
}

size_t my::TextureLayout::get_level_internal_byte_size(uint32_t level) const
{
    if (is_format_compressed(format))
    {
        return get_level_byte_size(level);
    }
    else
    {
        return get_level_data_width(level) * get_level_data_height(level) * get_level_depth(level)
            * format_internal_pixel_byte_size(format);
    }
}

bool my::GLInstance::is_texture_download_ready(uint64_t id) const
{
    auto it = _texture_downloads.find(id);
    if (it != _texture_downloads.end())
    {
        const TextureDownload& download = it->second;
        return download.downloaded || download.frame_number <= _latest_finished_frame;
    }
    else
    {
        return false;
    }
}

static my::TextureDownloadData download_texture(my::GLInstance& inst, my::TextureDownload& download)
{
    my::TextureDownloadData to_return = my::TextureDownloadData{my::TextureFormat::RGBA8, nullptr};

    size_t data_size =
        my::format_external_pixel_byte_size(download.format) * download.rect.w * download.rect.h;

    const auto* buffer = inst._buffers[my::get_resource_handle(download.buffer)];
    if (buffer)
    {
        assert(download.frame_number <= inst._latest_finished_frame);

        download.data.reset(new char[data_size]);
        inst.bind_buffer(GL_PIXEL_PACK_BUFFER, *buffer);

#if MYCELIUM_WEBGL_2
        myGetBufferSubData(GL_PIXEL_PACK_BUFFER, 0, data_size, download.data.get());
        GL_ERROR();
#else
        void* to_copy = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, data_size, GL_MAP_READ_BIT);
        GL_ERROR();
        memcpy(download.data.get(), to_copy, data_size);

        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        inst.unbind_buffer(GL_PIXEL_PACK_BUFFER);
        GL_ERROR();
#endif

        download.downloaded = true;
        to_return.format = download.format;
        to_return.data.swap(download.data);
    }
    else
    {
        MY_LOG_ERROR("Unknown texture download buffer");
    }

    return to_return;
}

my::TextureDownloadData my::GLInstance::retrieve_texture_download(uint64_t id)
{
    auto it = _texture_downloads.find(id);
    if (it != _texture_downloads.end())
    {
        if (it->second.buffer != ResourceHandle::null() && !it->second.downloaded)
        {
            TextureDownloadData to_return = download_texture(*this, it->second);
            _texture_downloads.erase(it);
            return to_return;
        }
        else
        {
            TextureDownloadData to_return;
            to_return.format = it->second.format;
            to_return.data.swap(it->second.data);
            _texture_downloads.erase(it);
            return to_return;
        }
    }
    else
    {
        return TextureDownloadData{TextureFormat::RGBA8, nullptr};
    }
}

void my::GLInstance::cancel_texture_download(uint64_t id)
{
    _texture_downloads.erase(id);
}

static void print_shader_compilation_log(GLuint shader)
{
    GLint status;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE)
    {
        GLint log_length;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);

        GLsizei length = log_length;
        auto log = std::make_unique<char[]>(length + 1);
        glGetShaderInfoLog(shader, length, &length, log.get());
        log[length] = 0;

        MY_LOG_ERROR("Shader compilation error: {}", log.get());
    }
}

void do_post_link_steps(my::GLInstance* inst, my::GLShader* ptr)
{
    assert(ptr->link_state == my::GLShader::Linking);

    if (ptr->in_initial_pool)
    {
        inst->_shaders_info.to_link_initial_done += 1;
    }
    inst->_shaders_info.root_shaders_linked += 1;
    ptr->link_state = my::GLShader::Linked;

    GLint status;
    // This forces synchronization if shader linkage was running asynchronously.
    glGetProgramiv(ptr->program, GL_LINK_STATUS, &status);
    if (status == GL_FALSE)
    {
        GLint log_length;
        glGetProgramiv(ptr->program, GL_INFO_LOG_LENGTH, &log_length);

        GLsizei length = log_length;
        auto log = std::make_unique<char[]>(length + 1);
        glGetProgramInfoLog(ptr->program, length, &length, log.get());
        log[length] = 0;

        MY_LOG_ERROR("Program linking log:\n{}", log.get());

        MY_LOG_ERROR("Vertex shader log:");
        print_shader_compilation_log(ptr->vertex_shader);

        MY_LOG_ERROR("Fragment shader log:");
        print_shader_compilation_log(ptr->fragment_shader);

        return;
    }

    for (uint32_t i = 0; i < ptr->uniform_block_count; ++i)
    {
        GLuint binding_index = ptr->uniform_block_defs[i].index;
        const char* name = ptr->uniform_block_defs[i].name;

        GLuint index = glGetUniformBlockIndex(ptr->program, name);
        if (index != GL_INVALID_INDEX)
        {
            glUniformBlockBinding(ptr->program, index, binding_index);
            GL_ERROR();
        }
        else
        {
            MY_LOG_WARNING("Invalid uniform block {}", name);
        }
    }

    inst->use_program(ptr->program);

    for (uint32_t i = 0; i < ptr->sampler_count; ++i)
    {
        GLint location = glGetUniformLocation(ptr->program, ptr->sampler_defs[i].name);
        if (location >= 0)
        {
            glUniform1i(location, ptr->sampler_defs[i].index);
            GL_ERROR();
        }
#ifndef MYCELIUM_DISABLE_UNKNOWN_SAMPLER_WARNINGS
        else
        {
            MY_LOG_WARNING(
                "Unknown sampler uniform {} at index {} for shader {}", ptr->sampler_defs[i].name,
                ptr->sampler_defs[i].index, ptr->name);
        }
#endif
    }

    ptr->base_instance_uniform_location = glGetUniformLocation(ptr->program, "my_BaseInstance");

    for (uint32_t i = 0; i < my::MaxFramebufferAttachments; ++i)
    {
        ptr->draw_buffers[i] = GL_NONE;
    }
    ptr->draw_buffer_count = 0;

    for (uint32_t i = 0; i < ptr->output_count; ++i)
    {
        GLint loc = glGetFragDataLocation(ptr->program, ptr->output_defs[i]);
        GL_ERROR();

        if (loc < 0)
        {
            MY_LOG_WARNING("Unknown frag data {}", ptr->output_defs[i]);
        }
        else if (loc >= my::MaxFramebufferAttachments)
        {
            MY_LOG_ERROR("Location out of bounds for frag data {}", ptr->output_defs[i]);
        }
        else
        {
            ptr->draw_buffers[loc] = GL_COLOR_ATTACHMENT0 + loc;
            ptr->draw_buffer_count = std::max(ptr->draw_buffer_count, loc + 1);
        }
    }

    ptr->draw_buffers_fingerprint =
        my::compute_draw_buffers_fingerprint({ptr->draw_buffers, (size_t)ptr->draw_buffer_count});
}

void my::GLInstance::configure_shaders_linking(const ShadersLinkingConfig& config)
{
    _linking_config = config;
}

bool my::GLInstance::find_shader_to_link(bool idle, uint64_t* shader_id)
{
    if (!_shaders_to_link_initially.empty())
    {
        auto it = _shaders_to_link_initially.begin();
        *shader_id = *it;
        _shaders_to_link_initially.erase(it);
        return true;
    }
    else if (!_shaders_to_link_soon.empty())
    {
        auto it = _shaders_to_link_soon.begin();
        *shader_id = *it;
        _shaders_to_link_soon.erase(it);
        return true;
    }
    else if (idle && !_shaders_to_link_when_idle.empty())
    {
        auto it = _shaders_to_link_when_idle.begin();
        *shader_id = *it;
        _shaders_to_link_when_idle.erase(it);
        return true;
    }
    else
    {
        return false;
    }
}

void my::GLInstance::advance_shaders_link(bool idle)
{
    // @Note: Firefox is the only major browser that doesn't cache compiled shader binaries
    // (https://bugzilla.mozilla.org/show_bug.cgi?id=918941) and does not support
    // KHR_parallel_shader_compile. Therefore, we always have to pay for the link times on the main
    // thread. :(

#if MYCELIUM_WEBGL_2
    for (auto it = _shaders_async_linking.begin(); it != _shaders_async_linking.end();)
    {
        GLShader* shader = _shaders[*it];
        assert(shader && shader->link_state == GLShader::Linking);

        if (my_js_check_program_link_completion_status(shader->program))
        {
            do_post_link_steps(this, shader);
            _shaders_async_linking.erase(it++);
        }
        else
        {
            it++;
        }
    }

    bool can_compile_parallel =
        _instance_info.has_parallel_shader_compile && _linking_config.allow_parallel_shader_compile;
#else
    constexpr bool can_compile_parallel = false;
#endif

    uint64_t shader_id;
    while (find_shader_to_link(idle, &shader_id))
    {
        GLShader* shader = _shaders[shader_id];
        if (shader && shader->link_state == GLShader::NotLinked)
        {
            link_shader(shader_id, shader, can_compile_parallel);
            if (!can_compile_parallel) break; // Only do one if can't do async
        }
    }
}

void my::GLInstance::link_shader(uint64_t shader_id, GLShader* ptr, bool async)
{
#if MYCELIUM_DEBUG_SHADERS_LINK_TIME
    double link_shader_start_ms = now_ms();
#endif
    assert(ptr->link_state == GLShader::NotLinked);

    glAttachShader(ptr->program, ptr->vertex_shader);
    glAttachShader(ptr->program, ptr->fragment_shader);
    GL_ERROR();

    for (uint32_t i = 0; i < ptr->attrib_count; ++i)
    {
        glBindAttribLocation(ptr->program, ptr->attrib_defs[i].index, ptr->attrib_defs[i].name);
    }
    GL_ERROR();

    glLinkProgram(ptr->program);
    GL_ERROR();

    ptr->link_state = GLShader::Linking;

#if MYCELIUM_WEBGL_2
    if (async)
    {
        _shaders_async_linking.insert(shader_id);
    }
    else
#endif
    {
        do_post_link_steps(this, ptr);
    }

#if MYCELIUM_DEBUG_SHADERS_LINK_TIME
    double link_shader_dur_ms = now_ms() - link_shader_start_ms;
    if (async)
    {
        MY_LOG_INFO("link_shader: linking '{}' asynchronously", ptr->name);
    }
    else
    {
        MY_LOG_INFO("link_shader: linked '{}' in {} ms", ptr->name, link_shader_dur_ms);
    }
#endif
}

void my::GLInstance::schedule_shader_to_be_linked_soon(uint64_t shader_id)
{
    GLShader* root_shader = _shaders[shader_id];
    if (!root_shader) return;

    uintptr_t group_id = root_shader->group_id;

    uint64_t head = _shader_group_first[group_id];
    assert(head != 0);

    while (head != 0)
    {
        GLShader* shader = _shaders[head];
        if (!shader) break;

        if (shader->link_state == GLShader::NotLinked)
        {
            _shaders_to_link_soon.insert(head);
        }

        head = shader->group_next;
    }
}

my::QueryTimeResult my::GLInstance::retrieve_elapsed_time(uint64_t id, QueryTimeStatus& status)
{
    if (_time_queries.find(id) == _time_queries.end())
    {
        MY_LOG_ERROR("Time query with ID {} not found", id);
        status = QueryTimeStatus::NOT_FOUND;
        return {};
    }

    GLint available = 0;

#ifdef MYCELIUM_WEBGL_2
    double elapsed_time = my_js_query_time_elapsed(_time_queries[id].gl_query);
    if (elapsed_time >= 0)
    {
        available = 1;
    }

    if (elapsed_time < -1)
    {
        MY_LOG_WARNING("GPU disjoint occured, all queries are invalid now");
        // GPU disjoint occured so we clear all queries id.
        _time_queries.clear();
        status = QueryTimeStatus::INVALID;
        return {};
    }
#else
    uint64_t elapsed_time = 0;
#    ifdef MYCELIUM_GL_ES
    glGetQueryObjectivEXT(_time_queries[id].gl_query, GL_QUERY_RESULT_AVAILABLE_EXT, &available);
#    else
    glGetQueryObjectiv(_time_queries[id].gl_query, GL_QUERY_RESULT_AVAILABLE, &available);
#    endif
    GL_ERROR();
    if (available == GL_TRUE)
    {
#    ifdef MYCELIUM_GL_ES
        glGetQueryObjectui64vEXT(_time_queries[id].gl_query, GL_QUERY_RESULT_EXT, &elapsed_time);
#    else
        glGetQueryObjectui64v(_time_queries[id].gl_query, GL_QUERY_RESULT, &elapsed_time);
#    endif
        GL_ERROR();
    }
#endif

    if (!available)
    {
        status = QueryTimeStatus::NOT_AVAILABLE;
        return {};
    }

    status = QueryTimeStatus::OK;

    my::QueryTimeResult result;
    result.name = _time_queries[id].name;
    result.elapsed_time = (float)(elapsed_time / 1000.0);
    return result;
}

void my::GLInstance::delete_query(uint64_t id)
{
    if (_time_queries.find(id) == _time_queries.end())
    {
        MY_LOG_ERROR("Error : Time query with ID {} not found", id);
        return;
    }

#ifdef MYCELIUM_WEBGL_2
    my_js_delete_query(_time_queries[id].gl_query);
#else
    glDeleteQueries(1, &_time_queries[id].gl_query);
#endif

    _time_queries.erase(id);
}

void my::GLInstance::activate_resource_allocation_reports()
{
    _store_resource_size_reports = true;
}

void my::GLInstance::deactivate_resource_allocation_reports()
{
    _store_resource_size_reports = false;
}

void my::GLInstance::push_resource_allocation_report(
    ResourceAllocationReport::Type type,
    ResourceHandle resource_handle,
    Resource::Type resource_type,
    size_t resource_size)
{
    if (_store_resource_size_reports)
    {
        ResourceAllocationReport report;
        report.type = type;
        report.resource_handle = resource_handle;
        report.resource_type = resource_type;
        report.resource_size = resource_size;

        _resource_size_reports.push_back(report);
    }
}

size_t my::GLInstance::get_resource_allocation_report_count() const
{
    return _resource_size_reports.size();
}

const my::ResourceAllocationReport* my::GLInstance::get_resource_allocation_reports()
{
    if (_resource_size_reports.empty()) return nullptr;
    return _resource_size_reports.data();
}

void my::GLInstance::clear_resource_allocation_reports()
{
    _resource_size_reports.clear();
}

void my::GLInstance::set_memory_limit(uint64_t size_bytes)
{
    _gpu_memory_limit = size_bytes;
}

my::Instance::GpuMemoryInfo my::GLInstance::get_memory_info()
{
    my::Instance::GpuMemoryInfo info;

    if (_gpu_memory_info_type == GpuMemoryInfoType::NVX_gpu_memory_info)
    {
#ifdef MYCELIUM_CORE_33
        GLint dedicated_vidmem;
        glGetIntegerv(GL_GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX, &dedicated_vidmem);
        info.total_device_memory = (uint64_t)dedicated_vidmem * 1024;

        GLint total_available_mem;
        glGetIntegerv(GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX, &total_available_mem);
        info.usable_memory = (uint64_t)total_available_mem * 1024;

        GLint current_available_mem;
        glGetIntegerv(GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX, &current_available_mem);
        info.free_memory = (uint64_t)current_available_mem * 1024;
#endif
    }
    else
    {
        info.total_device_memory = 0;
        info.usable_memory = 0;
        info.free_memory = 0;
    }

    return info;
}

bool my::GLInstance::begin_frame()
{
    while (!_last_frames_sync.empty())
    {
        auto frame_sync_and_number = _last_frames_sync.front();
        GLenum result = glClientWaitSync(frame_sync_and_number.first, 0, 0);
        GL_ERROR();
        if (result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED)
        {
            glDeleteSync(frame_sync_and_number.first);
            assert(_latest_finished_frame < frame_sync_and_number.second);
            _latest_finished_frame = frame_sync_and_number.second;
            _last_frames_sync.pop_front();
        }
        else
        {
            break;
        }
    }

    // Skip frames so that we only have a 1-frame overlap between CPU and GPU.
    return _last_frames_sync.size() <= 1;
}

void my::GLInstance::end_frame()
{
    _last_frames_sync.push_back(
        std::make_pair(glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0), _current_frame));
    GL_ERROR();
    glFlush();
    GL_ERROR();

    _current_frame += 1;

    _shaders_info.had_unlinked_shaders_last_frame =
        std::exchange(_shaders_info.had_unlinked_shaders_this_frame, false);
}
