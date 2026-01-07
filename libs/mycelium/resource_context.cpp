#if __EMSCRIPTEN__
#    include <GLES3/gl3.h>
#else
#    include <glad/glad.h>
#endif

#include "internal/conv.h"
#include "internal/instance.h"
#include "internal/log.h"
#include "mycelium/properties.h"

#include <string.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <span>

namespace my
{
static my::ResourceHandle create_buffer(GLInstance* my, const BufferResource& res)
{
    if (res.allow_allocation_failure && my->get_available_gpu_memory() < res.size)
    {
        return my::ResourceHandle::null();
    }

    uint64_t buffer_handle = my->_buffers.acquire({});
    GLBuffer* ptr = my->_buffers[buffer_handle];
    assert(ptr);

    ptr->target = buffer_resource_type_to_gl_target(res.type);
    ptr->size_bytes = res.size;

    GLenum usage = to_gl(res.usage);

    glGenBuffers(1, &ptr->buffer);
    my->bind_buffer(ptr->target, *ptr);
    glBufferData(ptr->target, res.size, res.data, usage);
    GL_ERROR();

    my::ResourceHandle res_handle = make_resource_handle(res.type, buffer_handle);

    my->_gpu_resources_size_bytes += res.size;
    my->push_resource_allocation_report(
        ResourceAllocationReport::Type::Allocation, res_handle, res.type, res.size);

    return res_handle;
}

static void realloc_buffer(GLInstance* my, ResourceHandle res_handle, const BufferResource& res)
{
    uint64_t buffer_handle = get_resource_handle(res_handle);

    GLBuffer* ptr = my->_buffers[buffer_handle];
    if (!ptr) return;

    GLenum usage = to_gl(res.usage);

    my->bind_buffer(ptr->target, *ptr);
    glBufferData(ptr->target, res.size, res.data, usage);
    GL_ERROR();

    my->_gpu_resources_size_bytes -= ptr->size_bytes;
    ptr->size_bytes = res.size;
    my->_gpu_resources_size_bytes += res.size;
    my->push_resource_allocation_report(
        ResourceAllocationReport::Type::Reallocation, res_handle, res.type, res.size);
}

static void destroy_resource(GLInstance* my, ResourceHandle res_handle)
{
    Resource::Type type = get_resource_type(res_handle);
    uint64_t simple_handle = get_resource_handle(res_handle);

    bool success = false;

    switch (type)
    {
        case Resource::VertexBuffer:
        case Resource::UniformBuffer:
        case Resource::TextureDownloadBuffer:
        case Resource::IndexBuffer:
        {
            GLBuffer* buffer_ptr = my->_buffers[simple_handle];
            if (buffer_ptr)
            {
                if (type == Resource::UniformBuffer)
                {
                    my->unbind_ubo_if_bound(buffer_ptr->buffer);
                }
                glDeleteBuffers(1, &buffer_ptr->buffer);
                my->_gpu_resources_size_bytes -= buffer_ptr->size_bytes;
                my->_buffers.release(simple_handle);
                success = true;
            }
            break;
        }
        case Resource::Shader:
        case Resource::ShaderDerivative:
        {
            // Never destroy shaders or pipelines
            MY_LOG_ERROR("Trying to destroy a shader");
            break;
        }
        case Resource::VertexInput:
        {
            GLVertexInput* ptr = my->_vertex_inputs[simple_handle];
            if (ptr)
            {
                my->unbind_vao_if_bound(ptr->vao);
                glDeleteVertexArrays(1, &ptr->vao);
                my->_vertex_inputs.release(simple_handle);
                success = true;
            }
            break;
        }
        case Resource::Texture:
        {
            GLTexture* texture = my->_textures[simple_handle];
            if (texture)
            {
                my->unbind_texture_if_bound(texture->texture);
                glDeleteTextures(1, &texture->texture);
                my->_gpu_resources_size_bytes -= texture->size_bytes;
                my->_textures.release(simple_handle);
                success = true;
            }
            break;
        }
        case Resource::Renderbuffer:
        {
            GLRenderbuffer* rb = my->_renderbuffers[simple_handle];
            if (rb)
            {
                glDeleteRenderbuffers(1, &rb->renderbuffer);
                my->_gpu_resources_size_bytes -= rb->size_bytes;
                my->_renderbuffers.release(simple_handle);
                success = true;
            }
            break;
        }
        case Resource::Sampler:
        {
            // We never actually destroy samplers
            my->_samplers.release(simple_handle);
            success = true;
            break;
        }
        case Resource::Framebuffer:
        {
            GLFramebuffer* fbo = my->_framebuffers[simple_handle];
            if (fbo)
            {
                my->unbind_draw_framebuffer_if_bound(simple_handle);
                my->unbind_read_framebuffer_if_bound(simple_handle);
                glDeleteFramebuffers(1, &fbo->fbo);
                my->_framebuffers.release(simple_handle);
                success = true;
            }
            break;
        }
        default:
        {
            if (!res_handle.is_null())
            {
                assert(false && "Unhandled case or uninitialized handle");
            }
        }
        break;
    }

    if (!res_handle.is_null())
    {
        if (success)
        {
            my->push_resource_allocation_report(
                ResourceAllocationReport::Type::Deallocation, res_handle, type, 0);
        }
        else
        {
            MY_LOG_ERROR("Cannot destroy resource: invalid handle");
        }
    }
}

#define STR2(X) #X
#define STR(X) STR2(X)

static GLuint create_shader_inner(
    const char* source,
    int len,
    GLenum type,
    const std::string& global_shader_defines)
{
    static constexpr const char* VERSION_STRING_LINE =
        "#version " STR(MYCELIUM_GLSL_VERSION) " " STR(MYCELIUM_GLSL_PROFILE) "\n";

    const GLchar* strings[] = {VERSION_STRING_LINE, global_shader_defines.c_str(), source};
    const GLint lengths[] = {
        (int)strlen(VERSION_STRING_LINE), (int)global_shader_defines.size(), len};

    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 3, strings, lengths);
    glCompileShader(shader);
    GL_ERROR();
    return shader;
}

static uint64_t create_shader(GLInstance* my, const ShaderResource& res)
{
    uint64_t shader_handle = my->_shaders.acquire(GLShader{});
    assert(shader_handle != 0); // Necessary for the group linked list

    GLShader* ptr = my->_shaders[shader_handle];
    assert(ptr);

    ptr->attrib_count = (uint32_t)std::min<size_t>(res.attribs.size(), MaxVertexAttributes);
    ptr->attrib_fingerprint.reset();
    for (uint32_t i = 0; i < ptr->attrib_count; ++i)
    {
        if (res.attribs[i].index >= MaxVertexAttributes)
        {
            MY_LOG_ERROR(
                "Shader {}: attribute {} has invalid index {}", res.name, res.attribs[i].name,
                res.attribs[i].index);
            continue;
        }

        ptr->attrib_fingerprint.set((size_t)res.attribs[i].index);
        ptr->attrib_defs[i] =
            my::IndexName{res.attribs[i].index, my->_intern.intern_as_str(res.attribs[i].name)};
    }

    ptr->active_ubos.reset();
    ptr->uniform_block_count =
        (uint32_t)std::min<size_t>(res.uniform_blocks.size(), MaxUniformBlocks);

    for (uint32_t i = 0; i < ptr->uniform_block_count; ++i)
    {
        if (res.uniform_blocks[i].index >= MaxUniformBlocks)
        {
            MY_LOG_ERROR(
                "Shader {}: uniform block {} has invalid binding index {}", res.name,
                res.uniform_blocks[i].name, res.uniform_blocks[i].index);
            continue;
        }

        ptr->uniform_block_defs[i] = IndexName{
            .index = res.uniform_blocks[i].index,
            .name = my->_intern.intern_as_str(res.uniform_blocks[i].name),
        };

        ptr->active_ubos.set(res.uniform_blocks[i].index);
    }

    ptr->active_textures.reset();
    ptr->sampler_count = (uint32_t)std::min<size_t>(res.samplers.size(), MaxTextureUnits);
    for (uint32_t i = 0; i < ptr->sampler_count; ++i)
    {
        assert(res.samplers[i].index < MaxTextureUnits);
        ptr->sampler_defs[i] =
            my::IndexName{res.samplers[i].index, my->_intern.intern_as_str(res.samplers[i].name)};
        ptr->active_textures.set(res.samplers[i].index);
    }

    ptr->output_count = (uint32_t)std::min<size_t>(res.outputs.size(), MaxFramebufferAttachments);
    for (uint32_t i = 0; i < ptr->output_count; ++i)
    {
        ptr->output_defs[i] = my->_intern.intern_as_str(res.outputs[i]);
    }

    ptr->vertex_shader = create_shader_inner(
        res.vertex_source, res.vertex_source_len, GL_VERTEX_SHADER, my->_global_shader_defines);

    ptr->fragment_shader = create_shader_inner(
        res.fragment_source, res.fragment_source_len, GL_FRAGMENT_SHADER,
        my->_global_shader_defines);

    ptr->program = glCreateProgram();

    ptr->name = res.name ? my->_intern.intern_as_str(res.name) : nullptr;
    ptr->link_hint = res.link_hint;
    ptr->link_state = GLShader::NotLinked;
    ptr->in_initial_pool = false;

    if (my->_current_frame == 0
        && (ptr->link_hint == ShaderLinkHint::Initial
            || my->_linking_config.force_compile_all_initial))
    {
        // We can't just change the link_hint because it would mean that FirstUseImmediate shaders
        // could be deferred. So instead use a flag to later increment the number of initial shaders
        // linked.
        ptr->in_initial_pool = true;
        my->_shaders_to_link_initially.insert(shader_handle);
        my->_shaders_info.to_link_initial += 1;
    }
    else if (my->_linking_config.allow_compile_all_when_idle)
    {
        my->_shaders_to_link_when_idle.insert(shader_handle);
    }

    {
        std::string group_name = res.name;
        auto it = group_name.find_first_of('_');
        if (it != std::string::npos)
        {
            group_name.resize(it);
        }

        uintptr_t group_id = my->_intern.intern(group_name.c_str());
        auto& head = my->_shader_group_first[group_id];

        // link the linked list
        ptr->group_next = std::exchange(head, shader_handle);
        ptr->group_id = group_id;
    }

    return shader_handle;
}

static void save_pipeline(
    GLInstance* my,
    Resource::Type type,
    uint64_t pipeline_id,
    const char* name)
{
    assert(type == Resource::Type::Shader || type == Resource::Type::ShaderDerivative);
    if (!name)
    {
        MY_LOG_ERROR("save_pipeline: Can't cache unamed shader");
        return;
    }

    my::GLPipeline* pipeline = my->_pipelines[pipeline_id];
    assert(pipeline);
    if (!pipeline) return;

    {
        uintptr_t intern = my->_intern.intern_or_null(name);
        if (intern && my->_cached_pipelines.count(intern) > 0)
        {
            MY_LOG_ERROR("save_pipeline: Duplicate shader name '{}'", name);
        }
    }
    uintptr_t intern = my->_intern.intern(name);
    my->_cached_pipelines[intern] = make_resource_handle(type, pipeline_id);
}

static uint64_t create_pipeline(
    GLInstance* my,
    uint64_t shader_id,
    const my::PipelineState& initial_state,
    Resource::Type type,
    const char* name)
{
    uint64_t pipeline_id = my->_pipelines.acquire(GLPipeline{shader_id, initial_state});

    my::GLShader* shader = my->_shaders[shader_id];
    assert(shader);
    if (my->_current_frame != 0 && shader->link_hint == ShaderLinkHint::Initial)
    {
        MY_LOG_ERROR(
            "create_pipeline: Trying to create a shader with initial linking after the first "
            "frame");
    }

    save_pipeline(my, type, pipeline_id, name);

    if (type == Resource::Type::Shader)
    {
        my->_shaders_info.root_shaders += 1;
    }
    else if (type == Resource::Type::ShaderDerivative)
    {
        my->_shaders_info.shader_derivatives += 1;
    }

    return pipeline_id;
}

static my::ResourceHandle create_pipeline(GLInstance* my, const ShaderResource& res)
{
    uint64_t shader_handle = create_shader(my, res);
    uint64_t pipeline_handle =
        create_pipeline(my, shader_handle, res.initial_state, res.type, res.name);

    my::ResourceHandle res_handle = make_resource_handle(res.type, pipeline_handle);

    my->push_resource_allocation_report(
        ResourceAllocationReport::Type::Allocation, res_handle, res.type, 0);

    return res_handle;
}

static my::ResourceHandle create_pipeline(GLInstance* my, const ShaderDerivativeResource& res)
{
    auto base_pipeline_type = get_resource_type(res.base_shader);
    if (base_pipeline_type != my::Resource::Shader
        && base_pipeline_type != my::Resource::ShaderDerivative)
    {
        MY_LOG_ERROR("create_pipeline: Base shader is not a shader");
        return my::ResourceHandle::null();
    }

    GLPipeline* base_pipeline = my->_pipelines[get_resource_handle(res.base_shader)];
    if (!base_pipeline)
    {
        MY_LOG_ERROR("create_pipeline: Invalid base shader");
        return my::ResourceHandle::null();
    }

    uint64_t pipeline_handle =
        create_pipeline(my, base_pipeline->shader, res.initial_state, res.type, res.name);

    my::ResourceHandle res_handle = make_resource_handle(res.type, pipeline_handle);

    my->push_resource_allocation_report(
        ResourceAllocationReport::Type::Allocation, res_handle, res.type, 0);

    return res_handle;
}

static my::ResourceHandle create_vertex_input(GLInstance* my, const VertexInputResource& res)
{
    uint64_t vi_handle = my->_vertex_inputs.acquire({});
    GLVertexInput* ptr = my->_vertex_inputs[vi_handle];
    assert(ptr);

    GLuint vao;
    glGenVertexArrays(1, &vao);
    my->bind_vao(vao);

    if (!res.indices.is_null())
    {
        if (get_resource_type(res.indices) != Resource::IndexBuffer)
        {
            MY_LOG_ERROR("Indices buffer is not an index buffer");
        }
        else
        {
            GLBuffer* buffer_ptr = my->_buffers[get_resource_handle(res.indices)];
            if (buffer_ptr)
            {
                my->bind_buffer(GL_ELEMENT_ARRAY_BUFFER, *buffer_ptr, true);
            }
        }
    }

    ptr->attrib_fingerprint.reset();
    for (const auto& stream : res.attribs)
    {
        if (stream.index >= MaxVertexAttributes)
        {
            MY_LOG_ERROR(
                "Vertex attribute index too large ({} >= {})", stream.index,
                fmt::underlying(MaxVertexAttributes));
            break;
        }

        ptr->attrib_fingerprint.set((size_t)stream.index);

        if (!stream.buffer.is_null())
        {
            if (get_resource_type(stream.buffer) != Resource::VertexBuffer)
            {
                assert(!"Buffer handle is not a vertex buffer");
                continue;
            }

            GLBuffer* buffer_ptr = my->_buffers[get_resource_handle(stream.buffer)];
            if (buffer_ptr)
            {
                glEnableVertexAttribArray(stream.index);
                GL_ERROR();
                my->bind_buffer(GL_ARRAY_BUFFER, *buffer_ptr);

                if (!format_integer(stream.format))
                {
                    glVertexAttribPointer(
                        stream.index, format_components(stream.format), format_type(stream.format),
                        to_gl(format_normalized(stream.format)), stream.stride,
                        (const void*)stream.offset);
                    GL_ERROR();
                }
                else
                {
                    glVertexAttribIPointer(
                        stream.index, format_components(stream.format), format_type(stream.format),
                        stream.stride, (const void*)stream.offset);
                    GL_ERROR();
                }

                GLuint divisor = 0;
                switch (stream.rate)
                {
                    case VertexRate::PerVertex: divisor = 0; break;
                    case VertexRate::PerInstance: divisor = 1; break;
                    case VertexRate::Per32Instances: divisor = 32; break;
                    case VertexRate::Constant:
                        // On Android, using the max value of GLuint does not work.
                        // (It works up to around 4,294,900,000.)
                        // Half of that should be plenty enough anyway.
                        divisor = std::numeric_limits<GLuint>::max() / 2;
                        break;
                    default: break;
                }
                glVertexAttribDivisor(stream.index, divisor);
                GL_ERROR();
            }
        }
    }

    ptr->vao = vao;
    ptr->is_indexed = !res.indices.is_null();

    my::ResourceHandle res_handle = make_resource_handle(res.type, vi_handle);

    my->push_resource_allocation_report(
        ResourceAllocationReport::Type::Allocation, res_handle, res.type, 0);

    return res_handle;
}

static my::ResourceHandle create_framebuffer(GLInstance* my, const FramebufferResource& res)
{
    const uint64_t fb_handle = my->_framebuffers.acquire({});
    GLFramebuffer* ptr = my->_framebuffers[fb_handle];
    assert(ptr);

    static const GLenum default_draw_buffer[] = {GL_COLOR_ATTACHMENT0};
    ptr->last_draw_buffers_fingerprint = compute_draw_buffers_fingerprint(default_draw_buffer);

    glGenFramebuffers(1, &ptr->fbo);
    my->bind_draw_framebuffer(fb_handle);

    for (const auto& att : res.attachments)
    {
        if (get_resource_type(att.texture_or_renderbuffer) == Resource::Texture)
        {
            const GLTexture* texture =
                my->_textures[get_resource_handle(att.texture_or_renderbuffer)];
            if (!texture) continue;

            switch (texture->layout.type)
            {
                case TextureLayout::Type2D:
                {
                    glFramebufferTexture2D(
                        GL_DRAW_FRAMEBUFFER, to_gl(att.bind_point), GL_TEXTURE_2D, texture->texture,
                        0);
                    GL_ERROR();
                    break;
                }
                default:
                {
                    MY_LOG_ERROR("We can only make framebuffers with 2D textures for now.");
                }
            }
        }
        else if (get_resource_type(att.texture_or_renderbuffer) == Resource::Renderbuffer)
        {
            const GLRenderbuffer* rb =
                my->_renderbuffers[get_resource_handle(att.texture_or_renderbuffer)];
            if (!rb) continue;

            glFramebufferRenderbuffer(
                GL_DRAW_FRAMEBUFFER, to_gl(att.bind_point), GL_RENDERBUFFER, rb->renderbuffer);
            GL_ERROR();
        }
        else
        {
            MY_LOG_ERROR("Framebuffer attachment is not a texture or renderbuffer");
            continue;
        }
    }

    GLenum status = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        const char* status_str = "unknown status";
        switch (status)
        {
            case GL_FRAMEBUFFER_UNDEFINED: status_str = "GL_FRAMEBUFFER_UNDEFINED"; break;
            case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
                status_str = "GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT";
                break;
            case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
                status_str = "GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT";
                break;
            case GL_FRAMEBUFFER_UNSUPPORTED: status_str = "GL_FRAMEBUFFER_UNSUPPORTED"; break;
            case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
                status_str = "GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE";
                break;
            default: break;
        }

        MY_LOG_ERROR("Framebuffer is incomplete: {} ({})", status_str, status);
    }

    my->bind_draw_framebuffer(0);

    my::ResourceHandle res_handle = make_resource_handle(res.type, fb_handle);

    my->push_resource_allocation_report(
        ResourceAllocationReport::Type::Allocation, res_handle, res.type, 0);

    return res_handle;
}

static size_t alloc_image(
    const TextureLayout& layout,
    GLenum target,
    int level,
    std::span<const std::byte> data)
{
    int dimension = 0;
    switch (layout.type)
    {
        case TextureLayout::Type3D:
        case TextureLayout::Array: dimension = 3; break;
        case TextureLayout::Type2D: dimension = 2; break;
        default: assert(!"Unhandled texture type"); return 0;
    }

    if (data.data() != nullptr)
    {
        if (data.size_bytes() < layout.get_level_byte_size(level))
        {
            MY_LOG_ERROR(
                "Not enough data provided for texture initialization: expected {}, got {}",
                layout.get_level_byte_size(level), data.size_bytes());
            data = {};
        }
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    GL_ERROR();

    if (dimension == 2)
    {
        uint32_t width = layout.get_level_width(level);
        uint32_t height = layout.get_level_height(level);
        if (is_format_compressed(layout.format))
        {
            glCompressedTexImage2D(
                target, level, format_internal(layout.format), width, height, 0, data.size_bytes(),
                (const void*)data.data());
            GL_ERROR();
        }
        else
        {
            glTexImage2D(
                target, level, format_internal(layout.format), width, height, 0,
                format_format(layout.format), format_type(layout.format), (const void*)data.data());
            GL_ERROR();
        }
    }
    else if (dimension == 3)
    {
        uint32_t width = layout.get_level_width(level);
        uint32_t height = layout.get_level_height(level);
        uint32_t depth = layout.get_level_depth(level);
        if (is_format_compressed(layout.format))
        {
            glCompressedTexImage3D(
                target, level, format_internal(layout.format), width, height, depth, 0,
                data.size_bytes(), (const void*)data.data());
            GL_ERROR();
        }
        else
        {
            glTexImage3D(
                target, level, format_internal(layout.format), width, height, depth, 0,
                format_format(layout.format), format_type(layout.format), (const void*)data.data());
            GL_ERROR();
        }
    }
    else
    {
        assert(!"Unhandled dimension");
    }

    return layout.get_level_internal_byte_size(level);
}

static void update_texture_layout(
    GLInstance* my,
    ResourceHandle res_handle,
    const TextureLayout& layout)
{
    uint64_t texture_handle = get_resource_handle(res_handle);

    GLTexture* ptr = my->_textures[texture_handle];
    if (!ptr) return;

    GLenum target = to_gl(layout.type);

    my->set_active_texture(0);
    my->bind_texture(0, target, ptr->texture);

    size_t data_size = 0;

    for (uint32_t level = 0; level < layout.levels; ++level)
    {
        data_size += alloc_image(layout, target, level, {});
    }

    my->_gpu_resources_size_bytes -= ptr->size_bytes;
    ptr->size_bytes = data_size;
    my->_gpu_resources_size_bytes += ptr->size_bytes;
    my->push_resource_allocation_report(
        ResourceAllocationReport::Type::Reallocation, res_handle, Resource::Type::Texture,
        data_size);
}

static my::ResourceHandle create_texture(GLInstance* my, const TextureResource& res)
{
    if (res.layout.width == 0 || res.layout.height == 0)
    {
        MY_LOG_ERROR("Invalid texture size: {}x{}", res.layout.width, res.layout.height);
    }

    bool generate_mipmaps = res.generate_mipmaps;
    if (generate_mipmaps && is_format_compressed(res.layout.format))
    {
        MY_LOG_ERROR("Cannot generate mipmaps for compressed textures");
        generate_mipmaps = false;
    }

    uint32_t level_count = generate_mipmaps ? 1 : res.layout.levels;

    size_t data_size = 0;

    for (uint32_t level = 0; level < level_count; ++level)
    {
        data_size += res.layout.get_level_internal_byte_size(level);
    }

    if (generate_mipmaps)
    {
        data_size *= 1.333333;
    }

    if (res.allow_allocation_failure && my->get_available_gpu_memory() < data_size)
    {
        return my::ResourceHandle::null();
    }

    uint64_t texture_handle = my->_textures.acquire(GLTexture{0, res.layout, 0});
    GLTexture* ptr = my->_textures[texture_handle];
    assert(ptr);

    GLenum target = to_gl(res.layout.type);

    GLuint tex;
    glGenTextures(1, &tex);
    my->set_active_texture(0);
    my->bind_texture(0, target, tex);

    for (uint32_t level = 0; level < level_count; ++level)
    {
        auto data = res.data.size() > level ? res.data[level] : std::span<const std::byte>{};
        alloc_image(res.layout, target, level, data);
    }

    if (generate_mipmaps)
    {
        glGenerateMipmap(target);
        GL_ERROR();
    }

    bool use_mipmaps = generate_mipmaps || res.layout.levels > 1;

    glTexParameteri(target, GL_TEXTURE_WRAP_S, to_gl(SamplerParams::Wrap::Clamp));
    glTexParameteri(target, GL_TEXTURE_WRAP_T, to_gl(SamplerParams::Wrap::Clamp));
    glTexParameteri(target, GL_TEXTURE_WRAP_R, to_gl(SamplerParams::Wrap::Clamp));
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, to_gl(SamplerParams::Filter::Nearest));
    glTexParameteri(
        target, GL_TEXTURE_MIN_FILTER,
        to_gl(SamplerParams::Filter::Nearest, SamplerParams::Filter::Nearest, use_mipmaps));
    GL_ERROR();

    ptr->texture = tex;
    ptr->size_bytes = data_size;

    my::ResourceHandle res_handle = make_resource_handle(res.type, texture_handle);

    my->_gpu_resources_size_bytes += data_size;
    my->push_resource_allocation_report(
        ResourceAllocationReport::Type::Allocation, res_handle, res.type, data_size);

    return res_handle;
}

static size_t alloc_renderbuffer(GLuint rb, TextureFormat format, uint32_t w, uint32_t h)
{
    assert(!is_format_compressed(format));

    glBindRenderbuffer(GL_RENDERBUFFER, rb);
    glRenderbufferStorage(GL_RENDERBUFFER, format_internal(format), w, h);
    GL_ERROR();
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    return my::format_internal_pixel_byte_size(format) * w * h;
}

static my::ResourceHandle create_renderbuffer(GLInstance* my, const RenderbufferResource& res)
{
    GLuint rb;
    glGenRenderbuffers(1, &rb);

    size_t data_size = alloc_renderbuffer(rb, res.format, res.width, res.height);

    uint64_t rb_handle = my->_renderbuffers.acquire(GLRenderbuffer{rb, res.format, 0});
    GLRenderbuffer* ptr = my->_renderbuffers[rb_handle];
    assert(ptr);

    ptr->size_bytes = data_size;

    my::ResourceHandle res_handle = make_resource_handle(res.type, rb_handle);

    my->_gpu_resources_size_bytes += data_size;
    my->push_resource_allocation_report(
        ResourceAllocationReport::Type::Allocation, res_handle, res.type, data_size);

    return res_handle;
}

static my::ResourceHandle create_sampler(GLInstance* my, const SamplerResource& res)
{
    GLSamplerParams params;
    params.wrap_s = to_gl(res.sampler.wrap_x);
    params.wrap_t = to_gl(res.sampler.wrap_y);
    params.wrap_r = to_gl(res.sampler.wrap_z);
    params.mag_filter = to_gl(res.sampler.mag_filter);
    params.min_filter = to_gl(res.sampler.min_filter, res.sampler.mipmap_filter, res.use_mipmaps);

    if (res.sampler.is_shadow)
    {
        params.compare_mode = GL_COMPARE_REF_TO_TEXTURE;
        params.compare_func = to_gl(res.sampler.compare);
    }
    else
    {
        params.compare_mode = GL_NONE;
        params.compare_func = GL_NONE;
    }

    GLuint gl_sampler = 0;

    auto it = my->_cached_samplers.find(params);
    if (it != my->_cached_samplers.end())
    {
        gl_sampler = it->second;
    }
    else
    {
        glGenSamplers(1, &gl_sampler);

        glSamplerParameteri(gl_sampler, GL_TEXTURE_WRAP_S, params.wrap_s);
        glSamplerParameteri(gl_sampler, GL_TEXTURE_WRAP_T, params.wrap_t);
        glSamplerParameteri(gl_sampler, GL_TEXTURE_WRAP_R, params.wrap_r);
        glSamplerParameteri(gl_sampler, GL_TEXTURE_MAG_FILTER, params.mag_filter);
        glSamplerParameteri(gl_sampler, GL_TEXTURE_MIN_FILTER, params.min_filter);

        if (params.compare_func != GL_NONE)
        {
            glSamplerParameteri(gl_sampler, GL_TEXTURE_COMPARE_MODE, params.compare_mode);
            glSamplerParameteri(gl_sampler, GL_TEXTURE_COMPARE_FUNC, params.compare_func);
        }
        else
        {
            glSamplerParameteri(gl_sampler, GL_TEXTURE_COMPARE_MODE, GL_NONE);
        }
        GL_ERROR();
        my->_cached_samplers.insert(std::make_pair(params, gl_sampler));
    }

    uint64_t sampler_handle = my->_samplers.acquire(gl_sampler);

    my::ResourceHandle res_handle = make_resource_handle(res.type, sampler_handle);

    my->push_resource_allocation_report(
        ResourceAllocationReport::Type::Allocation, res_handle, res.type, 0);

    return res_handle;
}

ResourceHandle GLInstance::alloc(const Resource* res_outer)
{
    assert(res_outer);

    ResourceHandle res_handle = ResourceHandle::null();

    switch (res_outer->type)
    {
        case Resource::VertexBuffer:
        case Resource::IndexBuffer:
        case Resource::UniformBuffer:
        case Resource::TextureDownloadBuffer:
        {
            res_handle = create_buffer(this, *(const BufferResource*)res_outer);
            break;
        }
        case Resource::Shader:
        {
            res_handle = create_pipeline(this, *(const ShaderResource*)res_outer);
            break;
        }
        case Resource::ShaderDerivative:
        {
            res_handle = create_pipeline(this, *(const ShaderDerivativeResource*)res_outer);
            break;
        }
        case Resource::VertexInput:
        {
            res_handle = create_vertex_input(this, *(const VertexInputResource*)res_outer);
            break;
        }
        case Resource::Texture:
        {
            res_handle = create_texture(this, *(const TextureResource*)res_outer);
            break;
        }
        case Resource::Renderbuffer:
        {
            res_handle = create_renderbuffer(this, *(const RenderbufferResource*)res_outer);
            break;
        }
        case Resource::Sampler:
        {
            res_handle = create_sampler(this, *(const SamplerResource*)res_outer);
            break;
        }
        case Resource::Framebuffer:
        {
            res_handle = create_framebuffer(this, *(const FramebufferResource*)res_outer);
            break;
        }
        default: assert(!"Unhandled resource type"); return ResourceHandle::null();
    }

    return res_handle;
}

void GLInstance::dealloc(ResourceHandle handle)
{
    if (handle.is_null()) return;
    destroy_resource(this, handle);
}

void GLInstance::realloc_buffer(ResourceHandle handle, const BufferResource* res)
{
    auto resource_type = get_resource_type(handle);
    if (!(resource_type == Resource::VertexBuffer || resource_type == Resource::IndexBuffer
          || resource_type == Resource::UniformBuffer
          || resource_type == Resource::TextureDownloadBuffer))
    {
        MY_LOG_ERROR("Resource is not a buffer");
        return;
    }

    my::realloc_buffer(this, handle, *res);
}

void GLInstance::update_texture_layout(ResourceHandle handle, const TextureLayout& layout)
{
    if (get_resource_type(handle) != Resource::Texture)
    {
        MY_LOG_ERROR("update_texture_layout: Resource is not a texture");
        return;
    }

    my::update_texture_layout(this, handle, layout);
}

void GLInstance::update_renderbuffer_size(ResourceHandle handle, uint32_t width, uint32_t height)
{
    if (get_resource_type(handle) != Resource::Renderbuffer)
    {
        MY_LOG_ERROR("update_renderbuffer_size: Resource is not a renderbuffer");
        return;
    }

    GLRenderbuffer* rb = _renderbuffers[get_resource_handle(handle)];
    if (!rb)
    {
        MY_LOG_ERROR("update_renderbuffer_size: Invalid renderbuffer");
    }

    size_t data_size = alloc_renderbuffer(rb->renderbuffer, rb->format, width, height);

    _gpu_resources_size_bytes -= rb->size_bytes;
    rb->size_bytes = data_size;
    _gpu_resources_size_bytes += data_size;
    push_resource_allocation_report(
        ResourceAllocationReport::Type::Reallocation, handle, Resource::Renderbuffer, data_size);
}

ResourceHandle GLInstance::retrieve_shader(const char* name) const
{
    uintptr_t intern = _intern.intern_or_null(name);
    auto it = _cached_pipelines.find(intern);
    if (!intern || it == _cached_pipelines.end())
    {
        MY_LOG_ERROR("retrieve_shader: No shader with name '{}'", name);
        return ResourceHandle::null();
    }
    return it->second;
}

} // namespace my
