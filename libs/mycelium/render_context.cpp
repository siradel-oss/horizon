// SPDX-FileCopyrightText: Copyright 2019 Siradel
// SPDX-License-Identifier: MIT

#include "mycelium/backend.h"

#include <assert.h>

#include <bitset>
#include <span>
#include <stdio.h>
#include <stdlib.h>
#include <string>

#if __EMSCRIPTEN__
#    include "mycelium/js/lib.h"

#    include <GLES3/gl3.h>
#else
#    include <glad/glad.h>
#endif

#include "internal/conv.h"
#include "internal/instance.h"
#include "internal/log.h"
#include "mycelium/properties.h"

namespace my
{

struct ColorTextureDownloadStartCommand
{
    uint64_t id;
    uint64_t fbo;
    Attachment attachment;
    Rect rect;
    TextureDownloadFormat format;
    ResourceHandle buffer;
};

struct ClearCommand
{
    uint32_t clear_count;
    const ClearTarget* values;
};

struct SetViewportCommand
{
    ViewportState state;
};

struct SetFramebufferCommand
{
    uint64_t fbo;
};

struct DrawCommand
{
    DrawBatchInfo info;
    uint64_t shader;
    uint64_t fbo;
    uint64_t vertex_input;
    uint64_t indices;
    uint32_t ubo_count;
    const UboBinding* ubos;
    uint32_t texture_count;
    const TextureBinding* textures;
};

struct UpdateTextureCommand
{
    uint64_t texture;
    TextureFormat format;
    int level;
    uint32_t x;
    uint32_t y;
    uint32_t z;
    uint32_t w;
    uint32_t h;
    uint32_t d;
    void* data;
};

struct BlitFramebuffersCommand
{
    uint64_t src;
    uint64_t dst;
    Rect src_rect;
    Rect dst_rect;
    AspectFlags aspects;
    Attachment src_attachment;
    uint32_t dst_attachment_count;
    const Attachment* dst_attachments;
    SamplerParams::Filter filter;
};

struct BeginQueryTimeCommand
{
    uint64_t query_id;
    const char* name;
};

struct EndQueryTimeCommand
{
    uint64_t query_id;
};

struct RenderCommand
{
    enum Type
    {
        Clear,
        Draw,
        UpdateBuffer,
        UpdateTexture,
        SetViewport,
        SetFramebuffer,
        BlitFramebuffers,
        ColorTextureDownloadStart,
        BeginQueryTime,
        EndQueryTime
    };

    Type type;
    uint64_t key;
    void* data;
};

void GLInstance::update_buffer(
    ResourceHandle buffer_handle,
    size_t offset,
    size_t size,
    const void* data)
{
    auto res_type = get_resource_type(buffer_handle);
    if (res_type != Resource::VertexBuffer && res_type != Resource::IndexBuffer
        && res_type != Resource::UniformBuffer && res_type != Resource::TextureDownloadBuffer)
    {
        MY_LOG_ERROR("update_buffer: Buffer is not a buffer");
        return;
    }

    if (size == 0) return;

    uint64_t buffer_id = get_resource_handle(buffer_handle);

    const GLBuffer* buffer = _buffers[buffer_id];
    if (buffer)
    {
        bind_buffer(buffer->target, *buffer);

        glBufferSubData(buffer->target, offset, size, data);
        GL_ERROR();
    }
}

void GLInstance::update_texture(
    ResourceHandle texture_handle,
    TextureFormat format,
    int level,
    uint32_t x,
    uint32_t y,
    uint32_t z,
    uint32_t w,
    uint32_t h,
    uint32_t d,
    std::span<const std::byte> data,
    TextureUpdateDataLayout data_layout)
{
    if (get_resource_type(texture_handle) != Resource::Texture)
    {
        MY_LOG_ERROR("update_texture: Texture is not a texture");
        return;
    }

    if (w == 0 || h == 0 || d == 0) return;

    const GLTexture* texture = _textures[get_resource_handle(texture_handle)];
    if (texture)
    {
        {
            auto sub_layout = texture->layout;
            sub_layout.levels = 1;
            sub_layout.width = w;
            sub_layout.height = h;
            sub_layout.depth = d;

            if (data.size_bytes() < sub_layout.get_level_byte_size(0))
            {
                MY_LOG_ERROR(
                    "update_texture: Not enough data provided: expected {}, got {}",
                    sub_layout.get_level_byte_size(0), data.size_bytes());
                return;
            }
        }

        GLenum target = to_gl(texture->layout.type);
        set_active_texture(0);
        bind_texture(0, target, texture->texture);

        uint32_t unpack_width = 0;
        uint32_t unpack_height = 0;
        if (data_layout == DataHasTargetTextureSize)
        {
            unpack_width = texture->layout.width;
            unpack_height = texture->layout.height;
        }

        switch (texture->layout.type)
        {
            case TextureLayout::Type2D:
            {
                if (unpack_width > 0)
                {
                    glPixelStorei(GL_UNPACK_ROW_LENGTH, unpack_width);
                }

                if (is_format_compressed(format))
                {
                    glCompressedTexSubImage2D(
                        target, level, x, y, w, h, format_internal(format), data.size_bytes(),
                        data.data());
                    GL_ERROR();
                }
                else
                {
                    glTexSubImage2D(
                        target, level, x, y, w, h, format_format(format), format_type(format),
                        data.data());
                    GL_ERROR();
                }

                if (unpack_width > 0)
                {
                    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
                }
                break;
            }
            case TextureLayout::Type3D:
            case TextureLayout::Array:
            {
                if (unpack_width > 0 || unpack_height > 0)
                {
                    glPixelStorei(GL_UNPACK_ROW_LENGTH, unpack_width);
                    glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, unpack_height);
                }

                if (is_format_compressed(format))
                {
                    glCompressedTexSubImage3D(
                        target, level, x, y, z, w, h, d, format_internal(format), data.size_bytes(),
                        data.data());
                    GL_ERROR();
                }
                else
                {
                    glTexSubImage3D(
                        target, level, x, y, z, w, h, d, format_format(format), format_type(format),
                        data.data());
                    GL_ERROR();
                }

                if (unpack_width > 0 || unpack_height > 0)
                {
                    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
                    glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, 0);
                }

                break;
            }
            default: assert(!"Unhandled texture type");
        }
    }
}

void GLInstance::clear(std::span<const ClearTarget> values)
{
    bool use_default_fbo = (_last_draw_framebuffer_handle == 0);

    GLenum draw_buffers[MaxFramebufferAttachments];
    GLsizei draw_buffer_count = 0;
    uint64_t draw_buffers_fingerprint = 0;
    for (uint32_t i = 0; i < MaxFramebufferAttachments; ++i)
    {
        draw_buffers[i] = GL_NONE;
    }

    for (const ClearTarget& value : values)
    {
        GLenum buffer;
        GLint draw_buffer = 0;

        bool clear_depth = false;
        bool clear_stencil = false;
        bool clear_color = false;

        switch (value.attachment)
        {
            case Attachment::Depth:
            {
                buffer = GL_DEPTH;
                clear_depth = true;
                break;
            }
            case Attachment::Stencil:
            {
                buffer = GL_STENCIL;
                clear_stencil = true;
                break;
            }
            case Attachment::DepthStencil:
            {
                buffer = GL_DEPTH_STENCIL;
                clear_depth = true;
                clear_stencil = true;
                break;
            }
            default:
            {
                buffer = GL_COLOR;
                draw_buffer = (int)value.attachment - (int)Attachment::Color0;
                clear_color = true;
                break;
            }
        }

        if (clear_depth)
        {
            glDepthMask(GL_TRUE);
            GL_ERROR();
            _current_state.depth.write = true;
        }

        if (clear_stencil)
        {
            glStencilMask(~0);
            GL_ERROR();
            _current_state.stencil.front.write_mask = ~0;
            _current_state.stencil.back.write_mask = ~0;
        }

        if (clear_color)
        {
            int to_reset = 0;

            if (use_default_fbo)
            {
                to_reset = 0;
                draw_buffers[0] = GL_COLOR_ATTACHMENT0;
                draw_buffer_count = 1;
            }
            else
            {
                int draw_buffer_index = (int)value.attachment - (int)Attachment::Color0;
                assert(draw_buffer_index < MaxFramebufferAttachments);

                to_reset = draw_buffer_index;
                draw_buffers[draw_buffer_index] = GL_COLOR_ATTACHMENT0 + draw_buffer_index;
                draw_buffer_count = draw_buffer_index + 1;
            }

            draw_buffers_fingerprint =
                compute_draw_buffers_fingerprint({draw_buffers, (size_t)draw_buffer_count});

            update_draw_buffers(
                {draw_buffers, (size_t)draw_buffer_count}, draw_buffers_fingerprint);

            draw_buffers[to_reset] = GL_NONE;

            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            GL_ERROR();
            _current_state.color_blend.mask = ColorBlendState::RGBA;
        }

        switch (value.value.type)
        {
            case ClearValue::ColorFloat:
            {
                glClearBufferfv(buffer, draw_buffer, value.value.color_float);
                GL_ERROR();
                break;
            }
            case ClearValue::ColorInt:
            {
                glClearBufferiv(buffer, draw_buffer, value.value.color_int);
                GL_ERROR();
                break;
            }
            case ClearValue::ColorUInt:
            {
                glClearBufferuiv(buffer, draw_buffer, value.value.color_uint);
                GL_ERROR();
                break;
            }
            case ClearValue::Depth:
            {
                glClearBufferfv(buffer, draw_buffer, &value.value.depth);
                GL_ERROR();
                break;
            }
            case ClearValue::Stencil:
            {
                glClearBufferiv(buffer, draw_buffer, &value.value.stencil);
                GL_ERROR();
                break;
            }
            case ClearValue::DepthStencil:
            {
                glClearBufferfi(buffer, draw_buffer, value.value.ds.depth, value.value.ds.stencil);
                GL_ERROR();
                break;
            }
        }
    }
}

void GLInstance::set_viewport(const ViewportState& state)
{
    apply_viewport_state(state);
}

void GLInstance::set_framebuffer(ResourceHandle fbo_handle, const ViewportState& state)
{
    set_viewport(state);

    if (get_resource_type(fbo_handle) == Resource::Framebuffer)
    {
        bind_draw_framebuffer(get_resource_handle(fbo_handle));
    }
    else if (fbo_handle.is_null())
    {
        bind_draw_framebuffer(0);
    }
    else
    {
        MY_LOG_ERROR("set_framebuffer: Framebuffer is not a framebuffer");
    }
}

static constexpr bool is_shader(my::Resource::Type type)
{
    return type == my::Resource::ShaderDerivative || type == my::Resource::Shader;
}

void GLInstance::draw(
    const DrawBatchInfo& info,
    ResourceHandle shader_handle,
    ResourceHandle vertex_input_handle,
    std::span<const UboBinding> ubos,
    std::span<const TextureBinding> textures)
{
    if (info.is_instanced && info.instances == 0)
    {
        return;
    }

    if (!is_shader(get_resource_type(shader_handle)))
    {
        MY_LOG_ERROR("draw: Shader is not a shader");
        return;
    }

    if (get_resource_type(vertex_input_handle) != Resource::VertexInput)
    {
        MY_LOG_ERROR("draw: Vertex input is not a vertex input");
        return;
    }

    for (const auto& ubo : ubos)
    {
        if (get_resource_type(ubo.buffer) != Resource::UniformBuffer)
        {
            MY_LOG_ERROR("draw: Uniform block buffer is not a uniform buffer");
            return;
        }
    }

    for (const auto& texture : textures)
    {
        if (get_resource_type(texture.texture) != Resource::Texture)
        {
            MY_LOG_ERROR("draw: Texture is not a texture");
            return;
        }
    }

    if (!bind_shader(get_resource_handle(shader_handle), info.cull_modifier))
    {
        _shaders_info.had_unlinked_shaders_this_frame = true;
        return;
    }

    const GLPipeline* pipeline = _pipelines[get_resource_handle(shader_handle)];
    if (!pipeline) return;

    const GLShader* shader = _shaders[pipeline->shader];
    if (!shader) return;

    const GLVertexInput* input = _vertex_inputs[get_resource_handle(vertex_input_handle)];
    if (input)
    {
        bind_vao(input->vao);

        if ((shader->attrib_fingerprint & input->attrib_fingerprint) != shader->attrib_fingerprint)
        {
            MY_LOG_WARNING(
                "draw: Vertex input missing attributes for shader {}: {:b}", shader->name,
                (shader->attrib_fingerprint & ~input->attrib_fingerprint).to_ulong());
        }
    }
    else
    {
        bind_vao(0);

        if (shader->attrib_fingerprint.any())
        {
            MY_LOG_WARNING(
                "draw: Vertex input missing for shader {}, which requires attributes",
                shader->name);
        }
    }

    update_draw_buffers(
        {shader->draw_buffers, (size_t)shader->draw_buffer_count},
        shader->draw_buffers_fingerprint);

    for (const auto& ubo : ubos)
    {
        if (ubo.index >= MaxUniformBlocks)
        {
            MY_LOG_ERROR(
                "draw: Uniform block index too large ({} >= {})", ubo.index,
                fmt::underlying(MaxUniformBlocks));
            continue;
        }

        if (!shader->active_ubos.test(ubo.index)) continue;

        const auto expected_size = shader->uniform_block_size_by_binding[ubo.index];
        if (ubo.size != expected_size)
        {
            MY_LOG_ERROR(
                "draw: Uniform block size mismatch for block {} in shader {}: expected {}, got {}",
                ubo.index, shader->name, expected_size, ubo.size);
            continue;
        }

        const GLBuffer* buffer = _buffers[get_resource_handle(ubo.buffer)];
        if (buffer)
        {
            bind_ubo(ubo.index, *buffer, ubo.offset, ubo.size);
        }

        // @Todo(perf) Maybe try to unbind UBOS that are not active, like textures?
    }

    for (const auto& texture : textures)
    {
        if (texture.index >= MaxTextureUnits)
        {
            MY_LOG_ERROR(
                "draw: Texture index too large ({} >= {})", texture.index,
                fmt::underlying(MaxTextureUnits));
            continue;
        }

        if (!shader->active_textures.test(texture.index)) continue;

        const GLTexture* gl_texture = _textures[get_resource_handle(texture.texture)];
        const GLuint* gl_sampler = _samplers[get_resource_handle(texture.sampler)];

        if (gl_texture)
        {
            bind_texture(texture.index, to_gl(gl_texture->layout.type), gl_texture->texture);

            if (gl_sampler)
            {
                bind_sampler(texture.index, *gl_sampler);
            }
            else
            {
                unbind_sampler(texture.index);
            }
        }
    }

    // For WebGL, we need to unbind all textures that won't be used
    // otherwise they might be bound in the current framebuffer as a
    // rendertarget and, even if they are not used by the shader,
    // Chrome and Firefox will complain.
    std::bitset<MaxTextureUnits> to_unbind = _bound_textures & (~shader->active_textures);

    if (to_unbind.any())
    {
        for (uint32_t i = 0; i < MaxTextureUnits; ++i)
        {
            if (to_unbind[i])
            {
                unbind_texture(i);
            }
        }
    }

    if (info.is_instanced && shader->base_instance_uniform_location >= 0)
    {
        glUniform1i(shader->base_instance_uniform_location, info.first_instance);
        GL_ERROR();
    }

    if (input->is_indexed)
    {
        if (!info.is_instanced)
        {
            glDrawElements(
                to_gl(info.type), info.count, to_gl(info.index_type),
                (const GLvoid*)(index_size(info.index_type) * info.index_offset));
            GL_ERROR();
        }
        else
        {
            glDrawElementsInstanced(
                to_gl(info.type), info.count, to_gl(info.index_type),
                (const GLvoid*)(index_size(info.index_type) * info.index_offset), info.instances);
            GL_ERROR();
        }
    }
    else
    {
        if (!info.is_instanced)
        {
            glDrawArrays(to_gl(info.type), 0, info.count);
            GL_ERROR();
        }
        else
        {
            glDrawArraysInstanced(to_gl(info.type), 0, info.count, info.instances);
            GL_ERROR();
        }
    }
}

void GLInstance::blit_framebuffers(
    ResourceHandle src_handle,
    Rect src_rect,
    Rect dst_rect,
    AspectFlags aspects,
    Attachment src_attachment,
    std::span<const Attachment> dst_attachments,
    SamplerParams::Filter filter)
{
    if (get_resource_type(src_handle) != Resource::Framebuffer && !src_handle.is_null())
    {
        MY_LOG_ERROR("blit_framebuffers: Source framebuffer is not a framebuffer");
        return;
    }

    if ((aspects & Aspect_All) == Aspect_None)
    {
        return;
    }

    const uint64_t read_fbo_handle = get_resource_handle(src_handle);
    bind_read_framebuffer(read_fbo_handle);

    const GLenum gl_filter = to_gl(filter);
    const GLbitfield gl_aspects = to_gl(aspects);

    if (aspects & Aspect_Color)
    {
        const bool use_default_fbo = (_last_draw_framebuffer_handle == 0);

        GLenum draw_buffers[MaxFramebufferAttachments];
        GLsizei draw_buffer_count = 0;
        uint64_t draw_buffers_fingerprint = 0;

        for (uint32_t i = 0; i < MaxFramebufferAttachments; ++i)
        {
            draw_buffers[i] = GL_NONE;
        }

        if (use_default_fbo)
        {
            draw_buffers[0] = GL_COLOR_ATTACHMENT0;
            draw_buffer_count = 1;
        }
        else
        {
            for (const auto attachment : dst_attachments)
            {
                if (attachment != Attachment::Depth && attachment != Attachment::Stencil
                    && attachment != Attachment::DepthStencil)
                {
                    int draw_buffer = (int)attachment - (int)Attachment::Color0;
                    assert(draw_buffer < MaxFramebufferAttachments);

                    draw_buffers[draw_buffer] = GL_COLOR_ATTACHMENT0 + draw_buffer;
                    draw_buffer_count = draw_buffer + 1;
                }
            }
        }

        draw_buffers_fingerprint =
            compute_draw_buffers_fingerprint({draw_buffers, (size_t)draw_buffer_count});

        update_draw_buffers({draw_buffers, (size_t)draw_buffer_count}, draw_buffers_fingerprint);

        if (_last_read_framebuffer_handle == 0)
        {
            update_read_buffer(GL_BACK);
        }
        else
        {
            update_read_buffer(to_gl(src_attachment));
        }
    }

    glBlitFramebuffer(
        src_rect.x, src_rect.y, src_rect.x + src_rect.w, src_rect.y + src_rect.h, dst_rect.x,
        dst_rect.y, dst_rect.x + dst_rect.w, dst_rect.y + dst_rect.h, gl_aspects, gl_filter);
    GL_ERROR();

    bind_read_framebuffer(read_fbo_handle);
}

TextureDownloadData GLInstance::color_texture_download_common(
    uint64_t download_id,
    ResourceHandle fb_handle,
    Attachment color_attachment,
    Rect rect,
    TextureDownloadFormat dl_format,
    ResourceHandle buffer_handle)
{
    if (!fb_handle.is_null() && get_resource_type(fb_handle) != Resource::Framebuffer)
    {
        MY_LOG_ERROR(
            "color_texture_download: Framebuffer for texture download is not a framebuffer");
        return {};
    }

    if ((int)color_attachment < (int)Attachment::_FirstColor
        || (int)color_attachment > (int)Attachment::_LastColor)
    {
        MY_LOG_ERROR(
            "color_texture_download: Attachment for texture download is not a color attachment");
        return {};
    }

    if (!buffer_handle.is_null()
        && get_resource_type(buffer_handle) != Resource::TextureDownloadBuffer)
    {
        MY_LOG_ERROR("color_texture_download: Buffer is not a texture download buffer");
        return {};
    }

    uint64_t read_fbo_handle = get_resource_handle(fb_handle);
    bind_read_framebuffer(read_fbo_handle);

    if (_last_read_framebuffer_handle == 0)
    {
        update_read_buffer(GL_BACK);
    }
    else
    {
        update_read_buffer(to_gl(color_attachment));
    }

    TextureFormat format = to_texture_format(dl_format);
    assert(!is_format_compressed(format));
    size_t data_size = format_external_pixel_byte_size(format) * rect.w * rect.h;

    TextureDownloadData sync_data;

    if (buffer_handle.is_null()) // Sync
    {
        sync_data.format = format;
        sync_data.data.reset(new char[data_size]);

        unbind_buffer(GL_PIXEL_PACK_BUFFER);
        glReadPixels(
            rect.x, rect.y, rect.w, rect.h, dl_format_format(dl_format), dl_format_type(dl_format),
            sync_data.data.get());
        GL_ERROR();
    }
    else // Async
    {
        auto dl_it = _texture_downloads.find(download_id);
        if (dl_it != _texture_downloads.end())
        {
            MY_LOG_WARNING(
                "color_texture_download: Texture download with ID {} overriden", download_id);
            return {};
        }

        TextureDownload& download = _texture_downloads[download_id];
        download.downloaded = false;
        download.format = format;
        download.rect = rect;
        download.buffer = buffer_handle;

        const auto* buffer = _buffers[get_resource_handle(buffer_handle)];
        if (buffer)
        {
            bind_buffer(GL_PIXEL_PACK_BUFFER, *buffer);
            glReadPixels(
                rect.x, rect.y, rect.w, rect.h, dl_format_format(dl_format),
                dl_format_type(dl_format), nullptr);
            GL_ERROR();
            unbind_buffer(GL_PIXEL_PACK_BUFFER);
        }
        else
        {
            MY_LOG_ERROR("color_texture_download_async: Unknown texture download buffer");
        }

        download.frame_number = _current_frame;
    }

    return sync_data;
}

TextureDownloadData GLInstance::color_texture_download_sync(
    ResourceHandle fb_handle,
    Attachment color_attachment,
    Rect rect,
    TextureDownloadFormat format)
{
    return color_texture_download_common(
        0, fb_handle, color_attachment, rect, format, my::ResourceHandle::null());
}

void GLInstance::color_texture_download_async(
    uint64_t download_id,
    ResourceHandle fb_handle,
    Attachment color_attachment,
    Rect rect,
    TextureDownloadFormat format,
    ResourceHandle buffer_handle)
{
    color_texture_download_common(
        download_id, fb_handle, color_attachment, rect, format, buffer_handle);
}

void GLInstance::begin_time_query(uint64_t query_id, const char* name)
{
    auto it = _time_queries.find(query_id);
    if (it != _time_queries.end())
    {
        MY_LOG_WARNING("begin_time_query: Time query with ID {} overriden", query_id);
        return;
    }

    GLuint gl_query_id;
#ifdef __EMSCRIPTEN__
    gl_query_id = my_js_begin_query_elapsed_time();
#elif MYCELIUM_GL_ES
    glGenQueriesEXT(1, &gl_query_id);
    GL_ERROR();
    glBeginQueryEXT(GL_TIME_ELAPSED_EXT, gl_query_id);
    GL_ERROR();
#else
    glGenQueries(1, &gl_query_id);
    GL_ERROR();
    glBeginQuery(GL_TIME_ELAPSED, gl_query_id);
    GL_ERROR();
#endif
    auto& time_query = _time_queries[query_id];
    time_query.gl_query = gl_query_id;
    time_query.name = name;
}

void GLInstance::end_time_query(uint64_t query_id)
{
    auto it = _time_queries.find(query_id);
    if (it == _time_queries.end())
    {
        MY_LOG_ERROR("end_time_query: Time query with ID {} not found", query_id);
        return;
    }

#ifdef __EMSCRIPTEN__
    my_js_end_time_query();
#elif MYCELIUM_GL_ES
    glEndQueryEXT(GL_TIME_ELAPSED_EXT);
    GL_ERROR();
#else
    glEndQuery(GL_TIME_ELAPSED);
    GL_ERROR();
#endif
}

} // namespace my
