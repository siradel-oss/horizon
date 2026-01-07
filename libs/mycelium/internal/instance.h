#pragma once

#ifdef __EMSCRIPTEN__
#    include <emscripten/emscripten.h>
#endif

#if MYCELIUM_WEBGL_2
#    include <GLES3/gl3.h>
#elif MYCELIUM_USE_GLAD
#    include <glad/glad.h>
#endif

#include "absl/container/flat_hash_map.h"
#include "intern_string.h"
#include "log.h"
#include "mycelium/backend.h"
#include "pool.h"

#include <limits>
#include <span>
#include <string>
#include <vector>

namespace my
{
inline const char* gl_error_string(GLenum e)
{
    switch (e)
    {
        case GL_INVALID_ENUM: return "INVALID_ENUM";
        case GL_INVALID_VALUE: return "INVALID_VALUE";
        case GL_INVALID_OPERATION: return "INVALID_OPERATION";
        case GL_INVALID_FRAMEBUFFER_OPERATION: return "INVALID_FRAMEBUFFER_OPERATION";
        case GL_OUT_OF_MEMORY: return "OUT_OF_MEMORY";
        default: return "Unknown";
    }
}

#ifndef NDEBUG
#    define GL_ERROR()                                                                      \
        do                                                                                  \
        {                                                                                   \
            GLenum error = glGetError();                                                    \
            if (error != GL_NO_ERROR)                                                       \
            {                                                                               \
                MY_LOG_ERROR("OpenGL error: {} ({})", ::my::gl_error_string(error), error); \
            }                                                                               \
        } while (false)
#else
#    define GL_ERROR()
#endif

struct GLVertexInput
{
    GLuint vao;
    bool is_indexed;
    std::bitset<MaxVertexAttributes> attrib_fingerprint;
};

struct GLBuffer
{
    GLuint buffer;
    GLenum target;
    size_t size_bytes;
};

struct GLTexture
{
    GLuint texture;
    TextureLayout layout;
    size_t size_bytes;
};

struct GLRenderbuffer
{
    GLuint renderbuffer;
    TextureFormat format;
    size_t size_bytes;
};

struct GLShader
{
    enum LinkState
    {
        NotLinked,
        Linking,
        Linked,
    };

    const char* name;
    ShaderLinkHint link_hint;
    bool in_initial_pool;
    LinkState link_state{NotLinked};

    // Group shaders together by prefix.
    // See the documentation of _shader_group_first.
    uintptr_t group_id;
    uint64_t group_next; // Next shader id in group.

    GLuint program;
    GLuint vertex_shader;
    GLuint fragment_shader;

    int draw_buffer_count;
    GLenum draw_buffers[MaxFramebufferAttachments];
    uint64_t draw_buffers_fingerprint;

    // One bit set per defined attribute index.
    std::bitset<MaxVertexAttributes> attrib_fingerprint;
    uint32_t attrib_count;
    IndexName attrib_defs[MaxVertexAttributes];

    uint32_t uniform_block_count;
    IndexName uniform_block_defs[MaxUniformBlocks];
    size_t uniform_block_size_by_binding[MaxUniformBlocks];

    uint32_t sampler_count;
    IndexName sampler_defs[MaxTextureUnits];

    std::bitset<MaxTextureUnits> active_textures;
    std::bitset<MaxUniformBlocks> active_ubos;

    uint32_t output_count;
    const char* output_defs[MaxFramebufferAttachments];

    int base_instance_uniform_location;
};

struct GLPipeline
{
    uint64_t shader;
    PipelineState initial_state;
};

struct GLFramebuffer
{
    GLuint fbo = 0;
    uint64_t last_draw_buffers_fingerprint = 0;
    GLenum last_read_buffer = 0;
};

struct GLSamplerParams
{
    GLenum wrap_s;
    GLenum wrap_t;
    GLenum wrap_r;
    GLenum min_filter;
    GLenum mag_filter;
    GLenum compare_mode;
    GLenum compare_func;

    constexpr bool operator==(const GLSamplerParams& other) const = default;

    template<typename H>
    friend H AbslHashValue(H h, const GLSamplerParams& p)
    {
        return H::combine(
            std::move(h), p.wrap_s, p.wrap_t, p.wrap_r, p.min_filter, p.mag_filter, p.compare_mode,
            p.compare_func);
    }
};

static uint8_t draw_buffer_fingerprint(GLenum value)
{
    // MUST FIT IN 4 BITS!!!
    switch (value)
    {
        case GL_NONE: return 0;
        case GL_COLOR_ATTACHMENT0: return 1;
        case GL_COLOR_ATTACHMENT1: return 2;
        case GL_COLOR_ATTACHMENT2: return 3;
        case GL_COLOR_ATTACHMENT3: return 4;
        case GL_COLOR_ATTACHMENT4: return 5;
        case GL_COLOR_ATTACHMENT5: return 6;
        case GL_BACK: return 15;
#if MYCELIUM_GL_DESKTOP
        case GL_BACK_LEFT: return 15;
#endif

        default: assert(!"Unhandled draw buffer value"); return 15;
    }
}

// The fingerprint is used to quickly check if we need a new call to
// glDrawBuffers by encoding the binding informations in a 64 bits uint value.
static uint64_t compute_draw_buffers_fingerprint(std::span<const GLenum> buffers)
{
    // We reserve 15 for GL_BACK(_LEFT) and 0 for GL_NONE.
    // We reserve the top 8 bits for "count".
    static_assert(
        (int)Attachment::_ColorCount < 14 && MaxFramebufferAttachments <= 14,
        "Not enough room for draw buffers fingerprint");
    assert(buffers.size() < MaxFramebufferAttachments);

    uint64_t fingerprint = 0;

    fingerprint |= (uint64_t)buffers.size() << 56;
    for (size_t i = 0; i < buffers.size(); ++i)
    {
        const uint8_t value = draw_buffer_fingerprint(buffers[i]);
        fingerprint |= ((uint64_t)value) << (i * 4);
    }

    return fingerprint;
}

struct TextureDownload
{
    bool downloaded = false;
    Rect rect;
    TextureFormat format;
    ResourceHandle buffer;
    std::unique_ptr<char[]> data;
    int frame_number;
};

struct TimeQuery
{
    GLuint gl_query;
    std::string name;
};

struct GLInstance : public Instance
{
    GLInstance();
    ~GLInstance() override = default;

    bool find_shader_to_link(bool idle, uint64_t* shader_id);
    void advance_shaders_link(bool idle) override;
    void configure_shaders_linking(const ShadersLinkingConfig&) override;
    void add_global_shader_define(const char* name, const char* value = "") override;

    const Info& get_info() const override { return _instance_info; }

    size_t get_uniform_buffer_offset_alignment() const override;
    bool is_texture_format_available(TextureFormat) const override;

    bool is_texture_download_ready(uint64_t id) const override;
    TextureDownloadData retrieve_texture_download(uint64_t id) override;
    void cancel_texture_download(uint64_t id) override;

    QueryTimeResult retrieve_elapsed_time(uint64_t id, QueryTimeStatus& status) override;
    void delete_query(uint64_t id) override;

    void activate_resource_allocation_reports() override;
    void deactivate_resource_allocation_reports() override;
    size_t get_resource_allocation_report_count() const override;
    const ResourceAllocationReport* get_resource_allocation_reports() override;
    void clear_resource_allocation_reports() override;

    void set_memory_limit(uint64_t size_bytes) override;
    GpuMemoryInfo get_memory_info() override;

    ShadersInfo get_shaders_info() const override { return _shaders_info; }

    // Resource context stuff

    ResourceHandle alloc(const Resource*) override;
    void dealloc(ResourceHandle) override;
    void realloc_buffer(ResourceHandle, const BufferResource*) override;
    void update_texture_layout(ResourceHandle, const TextureLayout&) override;
    void update_renderbuffer_size(ResourceHandle, uint32_t width, uint32_t height) override;
    ResourceHandle retrieve_shader(const char* name) const override;

    // Render context stuff

    void clear(std::span<const ClearTarget> values) override;

    void set_viewport(const ViewportState&) override;
    void set_framebuffer(ResourceHandle fbo, const ViewportState&) override;

    void update_buffer(ResourceHandle buffer, size_t offset, size_t size, const void* data)
        override;

    void update_texture(
        ResourceHandle texture,
        TextureFormat format,
        int level,
        uint32_t x,
        uint32_t y,
        uint32_t z,
        uint32_t w,
        uint32_t h,
        uint32_t d,
        std::span<const std::byte> data,
        TextureUpdateDataLayout data_layout) override;

    void draw(
        const DrawBatchInfo& info,
        ResourceHandle shader,
        ResourceHandle vertex_input,
        std::span<const UboBinding> ubos,
        std::span<const TextureBinding> textures) override;

    void blit_framebuffers(
        ResourceHandle src,
        Rect src_rect,
        Rect dst_rect,
        AspectFlags,
        Attachment src_attachment,
        std::span<const Attachment> dst_attachments,
        SamplerParams::Filter) override;

    TextureDownloadData color_texture_download_sync(
        ResourceHandle framebuffer,
        Attachment color_attachment,
        Rect rect,
        TextureDownloadFormat format) override;

    void color_texture_download_async(
        uint64_t download_id,
        ResourceHandle framebuffer,
        Attachment color_attachment,
        Rect rect,
        TextureDownloadFormat format,
        ResourceHandle buffer) override;

    TextureDownloadData color_texture_download_common(
        uint64_t download_id,
        ResourceHandle framebuffer,
        Attachment color_attachment,
        Rect rect,
        TextureDownloadFormat format,
        ResourceHandle buffer);

    void begin_time_query(uint64_t query_id, const char* name) override;

    void end_time_query(uint64_t query_id) override;

    int _current_frame = 0;
    int _latest_finished_frame = -1;
    std::deque<std::pair<GLsync, int>> _last_frames_sync; // Fence and frame number
    bool begin_frame() override;
    void end_frame() override;

    // Instance implementation

    GenPool<GLBuffer, 24, 32> _buffers;
    GenPool<GLShader, 24, 32> _shaders;
    GenPool<GLPipeline, 24, 32> _pipelines;
    GenPool<GLVertexInput, 24, 32> _vertex_inputs;
    GenPool<GLTexture, 24, 32> _textures;
    GenPool<GLRenderbuffer, 24, 32> _renderbuffers;
    GenPool<GLuint, 24, 32> _samplers;
    GenPool<GLFramebuffer, 24, 32> _framebuffers;

    absl::flat_hash_map<GLSamplerParams, GLuint> _cached_samplers;
    absl::flat_hash_map<uint64_t, TextureDownload> _texture_downloads;
    absl::flat_hash_map<uint64_t, TimeQuery> _time_queries;

    ShadersLinkingConfig _linking_config;
    absl::flat_hash_set<uint64_t> _shaders_to_link_initially; // At init
    absl::flat_hash_set<uint64_t> _shaders_to_link_soon;      // One per frame
    absl::flat_hash_set<uint64_t> _shaders_to_link_when_idle; // One per idle frame

    // Shaders are grouped per prefix (Cylinder_opaque => group Cylinder).
    // Grouped shaders are compiled together.
    // This is the "head" of the linked list of shaders. The next is given by GLShader.group_next.
    // The key is the interned group name.
    absl::flat_hash_map<uintptr_t, uint64_t>
        _shader_group_first; // Id of the head of the shader group list.

#if MYCELIUM_WEBGL_2
    absl::flat_hash_set<uint64_t> _shaders_async_linking; // Only used for async linking
#endif
    absl::flat_hash_map<uintptr_t, my::ResourceHandle> _cached_pipelines;

    bool _store_resource_size_reports;
    std::vector<ResourceAllocationReport> _resource_size_reports;

    InternString _intern;

    struct BoundTexture
    {
        GLenum target = 0;
        GLuint texture;
        GLuint sampler;
    };

    struct BoundUbo
    {
        GLuint buffer = 0;
        uint32_t offset = 0;
        uint32_t size = 0;
    };

    size_t _uniform_buffer_offset_alignment = 1;
    uint64_t _last_pipeline = ~0ULL;
    GLuint _last_program = ~0U;
    GLuint _last_vao = ~0U;
    uint64_t _last_draw_framebuffer_handle = 0;
    uint64_t _last_read_framebuffer_handle = 0;
    int _last_active_texture = ~0;
    uint64_t _last_default_framebuffer_draw_buffers_fingerprint = 0;
    GLenum _last_default_framebuffer_read_buffer = 0;
    std::bitset<MaxTextureUnits> _bound_textures = 0;
    BoundTexture _last_textures[MaxTextureUnits];
    BoundUbo _last_ubos[MaxUniformBlocks];
    PipelineState _current_state;
    CullModifier _last_cull_modifier = CullModifier::DontChange;
    ViewportState _current_viewport = {{0, 0, 0, 0}, {0, 0, 0, 0}};
    Info _instance_info;
    ShadersInfo _shaders_info;
    std::string _global_shader_defines;

    uint64_t _gpu_memory_limit = 0;
    uint64_t _gpu_resources_size_bytes = 0;

    enum class GpuMemoryInfoType
    {
        None,
        NVX_gpu_memory_info,
    };
    GpuMemoryInfoType _gpu_memory_info_type;

    uint64_t get_available_gpu_memory() const
    {
        if (_gpu_memory_limit == 0)
        {
            return std::numeric_limits<uint64_t>::max();
        }

        if (_gpu_resources_size_bytes >= _gpu_memory_limit)
        {
            return 0;
        }

        return _gpu_memory_limit - _gpu_resources_size_bytes;
    }

    void link_shader(uint64_t shader_id, GLShader*, bool async = false);
    void schedule_shader_to_be_linked_soon(uint64_t shader_id);

    inline bool bind_shader(uint64_t id, CullModifier cull_modifier)
    {
        const GLPipeline* pipeline = _pipelines[id];
        if (!pipeline) return false;

        GLShader* shader = _shaders[pipeline->shader];
        if (!shader) return false;

        if (shader->link_state == GLShader::NotLinked)
        {
            if (shader->link_hint == my::ShaderLinkHint::FirstUseImmediate)
            {
                link_shader(pipeline->shader, shader, false);
            }
            else
            {
                schedule_shader_to_be_linked_soon(pipeline->shader);
            }
        }
        if (shader->link_state != GLShader::Linked) return false;

        use_program(shader->program);
        if (id != _last_pipeline || cull_modifier != _last_cull_modifier)
        {
            apply_pipeline_state(pipeline->initial_state, cull_modifier);
            _last_pipeline = id;
            _last_cull_modifier = cull_modifier;
        }

        return true;
    }

    inline bool unbind_shader_if_bound(uint64_t id)
    {
        // You really want to call this function before
        // deleting the shader.

        const GLPipeline* pipeline = _pipelines[id];
        if (!pipeline) return false;

        const GLShader* shader = _shaders[pipeline->shader];
        if (!shader) return false;

        unuse_program_if_used(shader->program);
        if (id == _last_pipeline)
        {
            _last_pipeline = 0;
        }

        return true;
    }

    void update_read_buffer(GLenum buffer)
    {
        GLFramebuffer* fbo = _framebuffers[_last_read_framebuffer_handle];
        if (fbo)
        {
            if (fbo->last_read_buffer != buffer)
            {
                glReadBuffer(buffer);
                GL_ERROR();
                fbo->last_read_buffer = buffer;
            }
        }
        else
        {
            if (_last_default_framebuffer_read_buffer != buffer)
            {
                glReadBuffer(buffer);
                GL_ERROR();
                _last_default_framebuffer_read_buffer = buffer;
            }
        }
    }

    void update_draw_buffers(std::span<const GLenum> buffers, uint64_t fingerprint)
    {
        GLFramebuffer* fbo = _framebuffers[_last_draw_framebuffer_handle];
        if (fbo)
        {
            if (fbo->last_draw_buffers_fingerprint != fingerprint)
            {
                glDrawBuffers(static_cast<GLsizei>(buffers.size()), buffers.data());
                GL_ERROR();
                fbo->last_draw_buffers_fingerprint = fingerprint;
            }
        }
        else if (_last_draw_framebuffer_handle == 0)
        {
#if MYCELIUM_GL_DESKTOP
            static const GLenum back_buffers[] = {GL_BACK_LEFT};
#else
            static const GLenum back_buffers[] = {GL_BACK};
#endif
            static const uint64_t back_fingerprint = compute_draw_buffers_fingerprint(back_buffers);

            assert(buffers.size() == 1 && buffers[0] == GL_COLOR_ATTACHMENT0);

            if (_last_default_framebuffer_draw_buffers_fingerprint != back_fingerprint)
            {
                glDrawBuffers(1, back_buffers);
                GL_ERROR();
                _last_default_framebuffer_draw_buffers_fingerprint = back_fingerprint;
            }
        }
    }

    inline GLuint get_gl_framebuffer(uint64_t handle) const
    {
        const GLFramebuffer* fbo = _framebuffers[handle];
        return fbo ? fbo->fbo : 0;
    }

    inline void bind_draw_framebuffer(uint64_t handle)
    {
        if (_last_draw_framebuffer_handle != handle)
        {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, get_gl_framebuffer(handle));
            GL_ERROR();
            _last_draw_framebuffer_handle = handle;
        }
    }

    inline void unbind_draw_framebuffer_if_bound(uint64_t handle)
    {
        if (handle == _last_draw_framebuffer_handle)
        {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            GL_ERROR();
            _last_draw_framebuffer_handle = 0;

            static const GLenum buffers[] = {GL_COLOR_ATTACHMENT0};
            static const auto buffers_fingerprint = compute_draw_buffers_fingerprint(buffers);
            update_draw_buffers(buffers, buffers_fingerprint);
        }
    }

    inline void bind_read_framebuffer(uint64_t handle)
    {
        if (_last_read_framebuffer_handle != handle)
        {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, get_gl_framebuffer(handle));
            GL_ERROR();
            _last_read_framebuffer_handle = handle;
        }
    }

    inline void unbind_read_framebuffer_if_bound(uint64_t handle)
    {
        if (handle == _last_read_framebuffer_handle)
        {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            GL_ERROR();
            _last_read_framebuffer_handle = 0;
        }
    }

    inline void use_program(GLuint program)
    {
        if (_last_program != program)
        {
            glUseProgram(program);
            GL_ERROR();
            _last_program = program;
        }
    }

    inline void unuse_program_if_used(GLuint program)
    {
        if (_last_program == program)
        {
            glUseProgram(0);
            GL_ERROR();
            _last_program = 0;
        }
    }

    inline void bind_vao(GLuint vao)
    {
        if (_last_vao != vao)
        {
            glBindVertexArray(vao);
            GL_ERROR();
            _last_vao = vao;
        }
    }

    inline void unbind_vao_if_bound(GLuint vao)
    {
        if (_last_vao == vao)
        {
            glBindVertexArray(0);
            GL_ERROR();
            _last_vao = 0;
        }
    }

    inline void set_active_texture(int index)
    {
        if (index != _last_active_texture)
        {
            glActiveTexture(GL_TEXTURE0 + index);
            GL_ERROR();
            _last_active_texture = index;
        }
    }

    inline void bind_texture(int index, GLenum target, GLuint texture)
    {
        if (index >= MaxTextureUnits) return;

        if (target != 0
            && (_last_textures[index].target != target || _last_textures[index].texture != texture))
        {
            set_active_texture(index);
            glBindTexture(target, texture);
            GL_ERROR();
            _last_textures[index].target = target;
            _last_textures[index].texture = texture;

            if (texture == 0)
            {
                _bound_textures.reset(index);
            }
            else
            {
                _bound_textures.set(index);
            }
        }
    }

    inline void unbind_texture(int index)
    {
        if (index >= MaxTextureUnits) return;
        bind_texture(index, _last_textures[index].target, 0);
    }

    inline void unbind_texture_if_bound(GLuint texture)
    {
        for (int index = 0; index < MaxTextureUnits; ++index)
        {
            if (_last_textures[index].texture == texture)
            {
                unbind_texture(index);
            }
        }
    }

    inline void bind_sampler(int index, GLuint sampler)
    {
        if (index >= MaxTextureUnits) return;

        if (_last_textures[index].sampler != sampler)
        {
            glBindSampler(index, sampler);
            GL_ERROR();
            _last_textures[index].sampler = sampler;
        }
    }

    inline void unbind_sampler(int index)
    {
        if (index >= MaxTextureUnits) return;

        if (_last_textures[index].sampler != 0)
        {
            glBindSampler(index, 0);
            GL_ERROR();
            _last_textures[index].sampler = 0;
        }
    }

    inline void unbind_sampler_if_bound(GLuint sampler)
    {
        for (int index = 0; index < MaxTextureUnits; ++index)
        {
            if (_last_textures[index].sampler == sampler)
            {
                glBindSampler(index, 0);
                GL_ERROR();
                _last_textures[index].sampler = 0;
            }
        }
    }

    inline void bind_ubo(int index, const GLBuffer& buffer, uint32_t offset, uint32_t size)
    {
        if (index < 0 || index >= MaxUniformBlocks) return;

        auto& current = _last_ubos[index];
        if (current.buffer != buffer.buffer || current.offset != offset || current.size != size)
        {
            bind_buffer_range(GL_UNIFORM_BUFFER, index, buffer, offset, size);
            current.buffer = buffer.buffer;
            current.offset = offset;
            current.size = size;
        }
    }

    inline void bind_buffer(
        GLenum target,
        const GLBuffer& buffer,
        bool keep_vao_bound_if_index_buffer = false)
    {
        if (target != buffer.target)
        {
            MY_LOG_WARNING("Binding a buffer to a target it wasn't created with.");
        }

        // Binding an index buffer directly affects the bound VAO, if there is one.
        // Unlike vertex buffers, for which the VAO is only affected if
        // glVertexAttribPointer or glVertexAttribDivisor are called.
        // Because there are multiple places outside of building a VAO where index
        // buffers are bound, the current VAO must be unbound before binding the
        // index buffer.
        // Unless of course the VAO is under construction.
        if (target == GL_ELEMENT_ARRAY_BUFFER && !keep_vao_bound_if_index_buffer)
        {
            bind_vao(0);
        }

        glBindBuffer(target, buffer.buffer);
        GL_ERROR();
    }

    inline void unbind_buffer(GLenum target) const
    {
        assert(target != GL_UNIFORM_BUFFER);

        glBindBuffer(target, 0);

        GL_ERROR();
    }

    inline void unbind_ubo_if_bound(GLuint buffer)
    {
        for (int index = 0; index < MaxUniformBlocks; ++index)
        {
            if (_last_ubos[index].buffer == buffer)
            {
                glBindBuffer(GL_UNIFORM_BUFFER, 0);
                GL_ERROR();
                _last_ubos[index].buffer = 0;
            }
        }
    }

    inline void bind_buffer_range(
        GLenum target,
        int index,
        const GLBuffer& buffer,
        uint32_t offset,
        uint32_t size) const
    {
        if (target != buffer.target)
        {
            MY_LOG_WARNING("Binding a buffer to a target it wasn't created with.");
        }
        glBindBufferRange(target, index, buffer.buffer, offset, size);
        GL_ERROR();
    }

    void apply_pipeline_state(PipelineState state, CullModifier cull_mod);
    void apply_viewport_state(const ViewportState& state);
    void apply_rasterization_state(const RasterizationState& state);
    void apply_depth_state(const DepthState& state);
    void apply_stencil_state(const StencilState& state);
    void apply_stencil_state_face(GLenum face, const StencilState::Face& state) const;
    void apply_color_blend_state(const ColorBlendState& state);

    void push_resource_allocation_report(
        ResourceAllocationReport::Type type,
        ResourceHandle resource_handle,
        Resource::Type resource_type,
        size_t resource_size);
};

static const uint64_t HANDLE_BITS = 56;
static const uint64_t HANDLE_MASK = (1ULL << HANDLE_BITS) - 1;

inline Resource::Type get_resource_type(ResourceHandle handle)
{
    return (Resource::Type)(handle.handle >> HANDLE_BITS);
}

inline uint64_t get_resource_handle(ResourceHandle handle)
{
    return handle.handle & HANDLE_MASK;
}

inline ResourceHandle make_resource_handle(Resource::Type type, uint64_t handle)
{
    return ResourceHandle{((uint64_t)type << HANDLE_BITS) | handle};
}

} // namespace my
