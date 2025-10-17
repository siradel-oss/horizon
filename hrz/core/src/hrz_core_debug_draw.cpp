#include "hrz_core_debug_draw.h"

extern "C"
{
#include <microui/microui.h>
}

#include "hrz_core_global_flags.h"
#include "hrz_core_render.h"
#include "hrz_core_shaders.h"

#include <hrz_common_geo.h>
#include <hrz_common_profiling.h>
#include <hrz_common_proj.h>
#include <hrz_core_resources.h>
#include <hrz_fnd_array_view.h>
#include <hrz_fnd_flat_hash_map.h>
#include <hrz_fnd_flat_hash_set.h>
#include <hrz_fnd_hash.h>
#include <hrz_fnd_log.h>
#include <hrz_fnd_mem.h>

#include <stb_truetype.h>

#include <bitset>
#include <string>
#include <vector>

namespace dd = hrz::debug_draw;

namespace
{
constexpr lm::dvec3 BsCenter = {0.0, 0.0, 0.0};
constexpr double BsRadius = hrz::EARTH_RADIUS * 2.0;
constexpr size_t GroupCount = (size_t)dd::Group_Count;

enum
{
    SamplerTextInstanceData = 0,
    SamplerTextFontAtlas,

    SamplerTextureTexture = 0,
};

struct FontData
{
    my::ResourceHandle atlas_texture;
    my::ResourceHandle atlas_sampler;
    std::vector<stbtt_bakedchar> char_data;
    unsigned char min_char;
    size_t char_count;
};

struct TextVertex
{
    lm::vec2 pixel_position;
    lm::vec2 uv;
    uint32_t instance_index;
};

struct TextInstanceData
{
    lm::vec4 position_low_cs;
    lm::vec4 position_high;
    lm::vec4 color;
};

constexpr size_t TextDataTexturePixelSize = 16; // RGBA32 -> 4 x (4 bytes)
static_assert(sizeof(TextInstanceData) % TextDataTexturePixelSize == 0, "TextInstanceData size");

constexpr size_t TextDataTextureHeight = 64;
constexpr size_t TextDataTextureWidth = 64 * sizeof(TextInstanceData) / TextDataTexturePixelSize;
constexpr size_t TextMaxCommands = TextDataTextureWidth * TextDataTextureHeight
    * TextDataTexturePixelSize / sizeof(TextInstanceData);

struct TextRenderable : public my::Renderer::Renderable
{
    my::ResourceHandle vertex_buffer = my::ResourceHandle::null();
    my::ResourceHandle index_buffer = my::ResourceHandle::null();
    size_t vertex_buffer_capacity = 0;
    size_t index_buffer_capacity = 0;

    std::vector<TextInstanceData> instance_data;

    struct RenderData
    {
        my::ResourceHandle vertex_input = my::ResourceHandle::null();
        my::ResourceHandle visual_shader = my::ResourceHandle::null();
        my::ResourceHandle instance_data_texture_lazy = my::ResourceHandle::null();
        my::ResourceHandle instance_data_sampler = my::ResourceHandle::null();

        size_t index_count;
        FontData* font;
    } data;

    static void render_callback(
        uint32_t render_type,
        my::RenderContext* r,
        my::ResourceBinder* rb,
        const void* raw_user_data,
        const void* raw_data)
    {
        auto data = (const RenderData*)raw_data;

        my::ResourceHandle shader;

        switch (render_type)
        {
            case hrz::RenderVisual: shader = data->visual_shader; break;

            default: return;
        }

        auto batch = my::DrawBatchInfo(my::PrimitiveType::TriangleList, data->index_count)
                         .indexed(my::IndexType::UInt);

        my::TextureBinding textures[] = {
            {SamplerTextInstanceData, data->instance_data_texture_lazy,
             data->instance_data_sampler},
            {SamplerTextFontAtlas, data->font->atlas_texture, data->font->atlas_sampler},
        };

        rb->push_state();
        rb->bind(HRZ_ARRAY_COUNT(textures), textures);

        auto state = rb->get_current_state();

        r->draw(
            batch, shader, data->vertex_input, state.ubo_count, state.ubos, state.texture_count,
            state.textures);

        rb->pop_state();
    }

    void collect_render_info(my::Renderer::Queue& queue, const my::Renderer::Culler& culler)
        const override
    {
        if (data.index_count > 0)
        {
            queue.enqueue(hrz::RenderUiBin, render_callback, &data, BsCenter, BsRadius);
        }
    }
};

struct TextVertexData
{
    std::vector<TextVertex> vertices;
    std::vector<uint32_t> indices;
};

struct TextCommand
{
    std::string text;
    lm::dvec3 position;
    lm::vec4 color;
    dd::TextAlign align;
    dd::Space coordinate_space;
};

TextVertexData generate_text_vertex_data(
    std::span<const TextCommand> commands,
    const FontData& font_data)
{
    TextVertexData data;

    size_t vertex_count = 0;
    size_t index_count = 0;

    for (const auto& command : commands)
    {
        vertex_count += command.text.size() * 4;
        index_count += command.text.size() * 6;
    }

    data.vertices.resize(vertex_count);
    data.indices.resize(index_count);

    size_t glyph_index = 0;

    auto offset_vertices = [&](size_t from, size_t count, float horizontal_offset)
    {
        for (size_t i = from; i < from + count; i++)
        {
            data.vertices[i].pixel_position.x += horizontal_offset;
        }
    };

    for (size_t command_i = 0; command_i < commands.size(); command_i++)
    {
        const auto& text = commands[command_i].text;
        const auto align = commands[command_i].align;

        const auto first_glyph_index = glyph_index;
        lm::vec2 char_offset = {0, 0};

        for (size_t char_i = 0; char_i < text.size(); char_i++)
        {
            unsigned char c = text[char_i];

            if (c >= font_data.min_char
                && (size_t)c < (size_t)font_data.min_char + font_data.char_count)
            {
                stbtt_aligned_quad q;
                stbtt_GetBakedQuad(
                    font_data.char_data.data(), 512, 512, c - font_data.min_char, &char_offset.x,
                    &char_offset.y, &q, 1);

                // Y axis is flipped
                const float y1 = char_offset.y - (q.y1 - char_offset.y);
                const float y0 = y1 + (q.y1 - q.y0);
                q.y0 = y0;
                q.y1 = y1;

                const lm::vec2 v0 = {q.x0, q.y0};
                const lm::vec2 v1 = {q.x1, q.y0};
                const lm::vec2 v2 = {q.x1, q.y1};
                const lm::vec2 v3 = {q.x0, q.y1};

                const lm::vec2 uv0 = {q.s0, q.t0};
                const lm::vec2 uv1 = {q.s1, q.t0};
                const lm::vec2 uv2 = {q.s1, q.t1};
                const lm::vec2 uv3 = {q.s0, q.t1};

                data.vertices[glyph_index * 4 + 0] = {v0, uv0, (uint32_t)command_i};
                data.vertices[glyph_index * 4 + 1] = {v1, uv1, (uint32_t)command_i};
                data.vertices[glyph_index * 4 + 2] = {v2, uv2, (uint32_t)command_i};
                data.vertices[glyph_index * 4 + 3] = {v3, uv3, (uint32_t)command_i};

                data.indices[glyph_index * 6 + 0] = glyph_index * 4 + 0;
                data.indices[glyph_index * 6 + 1] = glyph_index * 4 + 1;
                data.indices[glyph_index * 6 + 2] = glyph_index * 4 + 2;
                data.indices[glyph_index * 6 + 3] = glyph_index * 4 + 0;
                data.indices[glyph_index * 6 + 4] = glyph_index * 4 + 2;
                data.indices[glyph_index * 6 + 5] = glyph_index * 4 + 3;

                glyph_index++;
            }
        }

        // const float text_length = data.vertices.at((first_glyph_index + string.size() * 4) -
        // 2).pixel_position.x;
        const float text_length = char_offset.x;

        switch (align)
        {
            case dd::TextAlign::Center:
                offset_vertices(first_glyph_index * 4, text.size() * 4, -text_length / 2.0f);
                break;
            case dd::TextAlign::Right:
                offset_vertices(first_glyph_index * 4, text.size() * 4, -text_length);
                break;
            default: break;
        }
    }

    return data;
}

void generate_text_instance_data(
    std::span<TextCommand> consumable_commands,
    std::vector<TextInstanceData>& instance_data)
{
    for (auto& command : consumable_commands)
    {
        TextInstanceData instance{};

        if (command.coordinate_space == dd::Space::LatLonAltRad)
        {
            command.position = {command.position.y, command.position.x, command.position.z};
            command.coordinate_space = dd::Space::LonLatAltRad;
        }

        if (command.coordinate_space == dd::Space::LonLatAltRad)
        {
            pl_transform_in_place_canonical(&hrz_proj::lonlat_rad_to_ecef, 1, &command.position.x);
            command.coordinate_space = dd::Space::Ecef;
        }
        else if (command.coordinate_space == dd::Space::WebMercator)
        {
            pl_transform_in_place_canonical(&hrz_proj::wmerc_to_ecef, 1, &command.position.x);
            command.coordinate_space = dd::Space::Ecef;
        }

        // The shader does not handle conversion from geographic coordinates.
        assert(
            command.coordinate_space != dd::Space::LatLonAltRad
            && command.coordinate_space != dd::Space::LonLatAltRad
            && command.coordinate_space != dd::Space::WebMercator);

        hrz::split_double(command.position.x, instance.position_low_cs.x, instance.position_high.x);
        hrz::split_double(command.position.y, instance.position_low_cs.y, instance.position_high.y);
        hrz::split_double(command.position.z, instance.position_low_cs.z, instance.position_high.z);

        instance.color = command.color;
        instance.position_low_cs.w = (float)(uint32_t)command.coordinate_space;

        instance_data.push_back(instance);
    }
}

struct PrimitiveRenderable : public my::Renderer::Renderable
{
    my::ResourceHandle vertex_buffer = my::ResourceHandle::null();
    size_t vertex_buffer_capacity = 0;

    struct RenderData
    {
        my::ResourceHandle shader = my::ResourceHandle::null();
        my::ResourceHandle uniform_buffer = my::ResourceHandle::null();
        my::ResourceHandle vertex_input = my::ResourceHandle::null();
        my::PrimitiveType primitive_type;
        uint32_t vertex_count = 0;
    } data;

    static void render_callback(
        uint32_t render_type,
        my::RenderContext* r,
        my::ResourceBinder* rb,
        const void* raw_user_data,
        const void* raw_data)
    {
        auto data = (const RenderData*)raw_data;

        if (data->vertex_count == 0) return;

        my::ResourceHandle shader;
        switch (render_type)
        {
            case hrz::RenderVisual: shader = data->shader; break;
            default: return;
        }

        auto batch = my::DrawBatchInfo(data->primitive_type, data->vertex_count);

        auto state = rb->get_current_state();

        r->draw(
            batch, shader, data->vertex_input, state.ubo_count, state.ubos, state.texture_count,
            state.textures);
    }

    void collect_render_info(my::Renderer::Queue& queue, const my::Renderer::Culler& culler)
        const override
    {
        if (data.vertex_count > 0)
        {
            queue.enqueue(hrz::RenderUiBin, render_callback, &data, BsCenter, BsRadius);
        }
    }
};

struct PrimitiveVertex
{
    lm::vec3 position_low;
    lm::vec3 position_high;
    lm::ubvec4 color;
    uint8_t coordinates;
};

struct PrimitiveCommand
{
    std::vector<lm::dvec3> points;
    lm::ubvec4 color;
    dd::Space coordinate_space;
};

std::vector<PrimitiveVertex> generate_primitive_vertex_data(
    std::span<PrimitiveCommand> consumable_commands)
{
    size_t vertex_count = 0;
    for (auto& command : consumable_commands)
    {
        vertex_count += command.points.size();
        if (!command.points.empty())
        {
            if (command.coordinate_space == dd::Space::LatLonAltRad)
            {
                for (auto& point : command.points)
                {
                    point = {point.y, point.x, point.z};
                }
                command.coordinate_space = dd::Space::LonLatAltRad;
            }

            if (command.coordinate_space == dd::Space::LonLatAltRad)
            {
                pl_transform_in_place_canonical(
                    &hrz_proj::lonlat_rad_to_ecef, command.points.size(), &command.points[0].x);
                command.coordinate_space = dd::Space::Ecef;
            }
            else if (command.coordinate_space == dd::Space::WebMercator)
            {
                pl_transform_in_place_canonical(
                    &hrz_proj::wmerc_to_ecef, command.points.size(), &command.points[0].x);
                command.coordinate_space = dd::Space::Ecef;
            }
        }
    }

    std::vector<PrimitiveVertex> vertex_data = {};
    vertex_data.resize(vertex_count);

    size_t vertex_index = 0;
    for (const auto& command : consumable_commands)
    {
        for (const auto& point : command.points)
        {
            // The shader does not handle conversion from geographic coordinates.
            assert(
                command.coordinate_space != dd::Space::LatLonAltRad
                && command.coordinate_space != dd::Space::LonLatAltRad
                && command.coordinate_space != dd::Space::WebMercator);

            hrz::split_double(
                point.x, vertex_data[vertex_index].position_low.x,
                vertex_data[vertex_index].position_high.x);
            hrz::split_double(
                point.y, vertex_data[vertex_index].position_low.y,
                vertex_data[vertex_index].position_high.y);
            hrz::split_double(
                point.z, vertex_data[vertex_index].position_low.z,
                vertex_data[vertex_index].position_high.z);

            vertex_data[vertex_index].color = command.color;
            vertex_data[vertex_index].coordinates = (uint8_t)command.coordinate_space;

            vertex_index++;
        }
    }

    return vertex_data;
}

struct Display
{
    bool pixel_grid_enabled;

    struct PixelGridRenderable : public my::Renderer::Renderable
    {
        struct RenderData
        {
            my::ResourceHandle vertex_buffer;
            my::ResourceHandle vertex_input;
            my::ResourceHandle shader;
        } data;

        static void render_callback(
            uint32_t render_type,
            my::RenderContext* r,
            my::ResourceBinder* rb,
            const void* raw_user_data,
            const void* raw_data)
        {
            auto data = (const RenderData*)raw_data;

            const auto batch = my::DrawBatchInfo(my::PrimitiveType::TriangleStrip, 3);
            const auto state = rb->get_current_state();

            r->draw(
                batch, data->shader, data->vertex_input, state.ubo_count, state.ubos,
                state.texture_count, state.textures);
        }

        void collect_render_info(my::Renderer::Queue& queue, const my::Renderer::Culler& culler)
            const override
        {
            queue.enqueue(hrz::RenderBin::RenderUiBin, render_callback, &data, BsCenter, BsRadius);
        }
    };

    PixelGridRenderable pixel_grid_renderable;

    void init_render(hrz::Render* render)
    {
        // Fullscreen quad
        {
            lm::vec2 vertices[] = {
                {-1, -1},
                {3, -1},
                {-1, 3},
            };

            my::BufferResource buf_res(my::BufferResource::Vertex);
            buf_res.size = sizeof(vertices);
            buf_res.data = vertices;
            buf_res.usage = my::UsageHint::Static;

            pixel_grid_renderable.data.vertex_buffer =
                render->rc->alloc(&buf_res, hrz::monitoring::systems::DevTools);

            my::VertexInputStream streams[] = {
                {0, pixel_grid_renderable.data.vertex_buffer, my::VertexFormat::Float32_2, 0, 0,
                 my::VertexRate::PerVertex}};

            my::VertexInputResource vi_res;
            vi_res.attrib_count = HRZ_ARRAY_COUNT(streams);
            vi_res.attribs = streams;

            pixel_grid_renderable.data.vertex_input =
                render->rc->alloc(&vi_res, hrz::monitoring::systems::DevTools);
        }

        // Pixel grid shader
        {
            static const my::IndexName attribs[] = {
                {0, "i_pos"},
            };

            static const char* outputs[] = {"o_color"};

            my::ShaderResource res{};
            res.name = hrz_shaders::PixelGrid_name;
            res.link_hint = my::ShaderLinkHint::FirstUseImmediate;
            res.vertex_source_len = hrz_shaders::PixelGrid_vert_len;
            res.vertex_source = hrz_shaders::PixelGrid_vert;
            res.fragment_source_len = hrz_shaders::PixelGrid_frag_len;
            res.fragment_source = hrz_shaders::PixelGrid_frag;
            res.attrib_count = 1;
            res.attribs = attribs;
            res.uniform_block_count = 0;
            res.uniform_blocks = nullptr;
            res.sampler_count = 0;
            res.samplers = nullptr;
            res.output_count = 1;
            res.outputs = outputs;
            res.initial_state.rasterization.cull_mode = my::RasterizationState::None;
            res.initial_state.depth.test = false;
            res.initial_state.depth.write = false;
            res.initial_state.stencil.enable = false;
            res.initial_state.color_blend.enable = false;
            res.initial_state.color_blend.mask = my::ColorBlendState::RGBA;

            pixel_grid_renderable.data.shader =
                render->rc->alloc(&res, hrz::monitoring::systems::DevTools);
        }
    }

    void deinit_render(hrz::Render* render)
    {
        render->rc->dealloc(pixel_grid_renderable.data.vertex_buffer);
        render->rc->dealloc(pixel_grid_renderable.data.vertex_input);
    }

    void draw(my::RenderContext* r)
    {
        if (pixel_grid_enabled)
        {
            const auto info = my::DrawBatchInfo(my::PrimitiveType::TriangleList, 3);
            r->draw(
                info, pixel_grid_renderable.data.shader, pixel_grid_renderable.data.vertex_input, 0,
                nullptr, 0, nullptr);
        }
    }
};

struct TextureCommand
{
    my::ResourceHandle texture;
};

struct TextureRenderable : public my::Renderer::Renderable
{
    struct RenderData
    {
        my::ResourceHandle vertex_buffer;
        my::ResourceHandle vertex_input;
        my::ResourceHandle shader;
        my::ResourceHandle texture;
        my::ResourceHandle sampler;
    } data;

    void init_render(hrz::Render* render)
    {
        assert(render);

        // Fullscreen quad.
        lm::vec2 vertices[] = {
            {-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1},
        };

        my::BufferResource buf_res(my::BufferResource::Vertex);
        buf_res.size = sizeof(vertices);
        buf_res.data = vertices;
        buf_res.usage = my::UsageHint::Static;
        data.vertex_buffer = render->rc->alloc(&buf_res, hrz::monitoring::systems::DevTools);

        my::VertexInputStream streams[] = {
            {0, data.vertex_buffer, my::VertexFormat::Float32_2, 0, 0, my::VertexRate::PerVertex}};

        my::VertexInputResource vi_res;
        vi_res.attrib_count = HRZ_ARRAY_COUNT(streams);
        vi_res.attribs = streams;
        data.vertex_input = render->rc->alloc(&vi_res, hrz::monitoring::systems::DevTools);

        my::SamplerResource sampler_res;
        sampler_res.sampler.min_filter = my::SamplerParams::Filter::Nearest;
        sampler_res.sampler.mag_filter = my::SamplerParams::Filter::Nearest;
        sampler_res.sampler.wrap_x = my::SamplerParams::Wrap::Clamp;
        sampler_res.sampler.wrap_y = my::SamplerParams::Wrap::Clamp;
        sampler_res.sampler.is_shadow = false;
        sampler_res.use_mipmaps = false;
        data.sampler = render->rc->alloc(&sampler_res, hrz::monitoring::systems::DevTools);

        {
            static const my::IndexName attribs[] = {{0, "i_pos"}};
            static const char* outputs[] = {"o_color"};
            static const my::IndexName samplers[] = {{SamplerTextureTexture, "u_texture"}};
            static const my::IndexName ubos[] = {{hrz::UboFrame, "Frame"}};

            my::ShaderResource res;
            res.name = hrz_shaders::DebugTexture_name;
            res.link_hint = my::ShaderLinkHint::FirstUseImmediate;
            res.vertex_source_len = hrz_shaders::DebugTexture_vert_len;
            res.vertex_source = hrz_shaders::DebugTexture_vert;
            res.fragment_source_len = hrz_shaders::DebugTexture_frag_len;
            res.fragment_source = hrz_shaders::DebugTexture_frag;
            res.attrib_count = 1;
            res.attribs = attribs;
            res.uniform_block_count = 1;
            res.uniform_blocks = ubos;
            res.sampler_count = 1;
            res.samplers = samplers;
            res.output_count = 1;
            res.outputs = outputs;
            res.initial_state.rasterization.cull_mode = my::RasterizationState::None;
            res.initial_state.depth.test = false;
            res.initial_state.depth.write = false;
            res.initial_state.stencil.enable = false;
            res.initial_state.color_blend.enable = false;
            res.initial_state.color_blend.mask = my::ColorBlendState::RGBA;
            data.shader = render->rc->alloc(&res, hrz::monitoring::systems::DevTools);
        }
    }

    void deinit_render(hrz::Render* render)
    {
        render->rc->dealloc(data.vertex_buffer);
        render->rc->dealloc(data.vertex_input);
        render->rc->dealloc(data.sampler);
    }

    static void render_callback(
        uint32_t render_type,
        my::RenderContext* r,
        my::ResourceBinder* rb,
        const void* raw_user_data,
        const void* raw_data)
    {
        auto data = (const RenderData*)raw_data;

        rb->push_state();

        const auto batch = my::DrawBatchInfo(my::PrimitiveType::TriangleList, 6);

        my::TextureBinding texture_bindings[] = {
            {SamplerTextureTexture, data->texture, data->sampler}};
        rb->bind(HRZ_ARRAY_COUNT(texture_bindings), texture_bindings);

        const auto state = rb->get_current_state();

        r->draw(
            batch, data->shader, data->vertex_input, state.ubo_count, state.ubos,
            state.texture_count, state.textures);

        rb->pop_state();
    }

    void collect_render_info(my::Renderer::Queue& queue, const my::Renderer::Culler& culler)
        const override
    {
        static constexpr lm::dvec3 BsCenter = {0.0, 0.0, 0.0};
        static constexpr double BsRadius = hrz::EARTH_RADIUS * 2.0;
        queue.enqueue(hrz::RenderBin::RenderUiBin, render_callback, &data, BsCenter, BsRadius);
    }
};

std::vector<PrimitiveCommand> g_line_commands;
std::vector<PrimitiveCommand> g_point_commands;
std::vector<PrimitiveCommand> g_triangle_commands;
std::vector<TextCommand> g_text_commands;
std::optional<my::ResourceHandle> g_texture = std::nullopt;

std::bitset<GroupCount> g_visibility_mask = ~0;

} // namespace

namespace hrz
{
struct DebugDrawSystem
{
    my::ResourceHandle primitive_shader;
    my::ResourceHandle text_shader;

    FontData font_data;

    PrimitiveRenderable lines_renderable;
    PrimitiveRenderable points_renderable;
    PrimitiveRenderable triangles_renderable;
    TextRenderable text_renderable;
    TextureRenderable texture_renderable;

    Display display;

    bool rendered_last_frame;
};

namespace debug_draw
{
namespace
{
void init_text_renderable(
    std::span<TextCommand> consumable_commands,
    TextRenderable& renderable,
    DebugDrawSystem* dd,
    Render* render)
{
    static constexpr size_t max_command_count = TextMaxCommands;
    if (consumable_commands.size() > max_command_count)
        consumable_commands = consumable_commands.first(max_command_count);

    TextVertexData data = generate_text_vertex_data(consumable_commands, dd->font_data);

    my::BufferResource vb_res(my::BufferResource::BufferType::Vertex);
    vb_res.size = data.vertices.size() * sizeof(TextVertex);
    vb_res.usage = my::UsageHint::Updatable;
    vb_res.data = data.vertices.data();

    my::ResourceHandle vertex_buffer = render->rc->alloc(&vb_res, monitoring::systems::DevTools);

    my::VertexInputStream streams[] = {
        {0, vertex_buffer, my::VertexFormat::Float32_2, 0, sizeof(TextVertex),
         my::VertexRate::PerVertex},
        {1, vertex_buffer, my::VertexFormat::Float32_2, sizeof(lm::vec2), sizeof(TextVertex),
         my::VertexRate::PerVertex},
        {2, vertex_buffer, my::VertexFormat::UInt32, 2 * sizeof(lm::vec2), sizeof(TextVertex),
         my::VertexRate::PerVertex}};

    my::BufferResource ib_res(my::BufferResource::BufferType::Index);
    ib_res.size = data.indices.size() * sizeof(uint32_t);
    ib_res.usage = my::UsageHint::Updatable;
    ib_res.data = data.indices.data();

    my::ResourceHandle index_buffer = render->rc->alloc(&ib_res, monitoring::systems::DevTools);

    my::VertexInputResource vi_res;
    vi_res.indices = index_buffer;
    vi_res.attrib_count = HRZ_ARRAY_COUNT(streams);
    vi_res.attribs = streams;

    my::ResourceHandle vertex_input = render->rc->alloc(&vi_res, monitoring::systems::DevTools);

    my::SamplerResource s_res;
    s_res.sampler.min_filter = my::SamplerParams::Filter::Nearest;
    s_res.sampler.mag_filter = my::SamplerParams::Filter::Nearest;
    s_res.sampler.wrap_x = my::SamplerParams::Wrap::Clamp;
    s_res.sampler.wrap_y = my::SamplerParams::Wrap::Clamp;
    s_res.sampler.wrap_z = my::SamplerParams::Wrap::Clamp;
    s_res.use_mipmaps = false;

    my::ResourceHandle instance_sampler = render->rc->alloc(
        &s_res, monitoring::systems::DevTools, {{"contents"_ss, "debug text instance sampler"_ss}});

    renderable.vertex_buffer = vertex_buffer;
    renderable.vertex_buffer_capacity = vb_res.size;
    renderable.index_buffer = index_buffer;
    renderable.index_buffer_capacity = ib_res.size;
    renderable.data.vertex_input = vertex_input;
    renderable.data.index_count = data.indices.size();
    renderable.data.instance_data_texture_lazy = my::ResourceHandle::null();
    renderable.data.instance_data_sampler = instance_sampler;
    renderable.data.font = &dd->font_data;
    renderable.data.visual_shader = dd->text_shader;
}

void update_text_renderable(
    std::span<TextCommand> consumable_commands,
    TextRenderable& renderable,
    const FontData& font_data,
    Render* render)
{
    static constexpr size_t max_command_count = TextMaxCommands;
    if (consumable_commands.size() > max_command_count)
        consumable_commands = consumable_commands.first(max_command_count);

    TextVertexData data = generate_text_vertex_data(consumable_commands, font_data);

    // Update vertex buffer contents
    size_t min_vb_capacity = data.vertices.size() * sizeof(TextVertex);
    if (renderable.vertex_buffer_capacity < min_vb_capacity)
    {
        size_t updated_buffer_capacity =
            std::max(min_vb_capacity, 2 * renderable.vertex_buffer_capacity);
        data.vertices.reserve(updated_buffer_capacity / sizeof(TextVertex));

        data.vertices.reserve(updated_buffer_capacity / sizeof(TextVertex));

        my::BufferResource vb_res(my::BufferResource::BufferType::Vertex);
        vb_res.size = updated_buffer_capacity;
        vb_res.usage = my::UsageHint::Dynamic;
        vb_res.data = data.vertices.data();

        render->rc->realloc_buffer(renderable.vertex_buffer, &vb_res);
        renderable.vertex_buffer_capacity = updated_buffer_capacity;
    }
    else
    {
        render->my->update_buffer(
            renderable.vertex_buffer, 0, min_vb_capacity, data.vertices.data());
    }

    // Update index buffer contents
    size_t min_ib_capacity = data.indices.size() * sizeof(uint32_t);
    if (renderable.index_buffer_capacity < min_ib_capacity)
    {
        size_t updated_buffer_capacity =
            std::max(min_ib_capacity, 2 * renderable.index_buffer_capacity);
        data.indices.reserve(updated_buffer_capacity / sizeof(uint32_t));

        data.indices.reserve(updated_buffer_capacity / sizeof(uint32_t));

        my::BufferResource ib_res(my::BufferResource::BufferType::Index);
        ib_res.size = updated_buffer_capacity;
        ib_res.usage = my::UsageHint::Dynamic;
        ib_res.data = data.indices.data();

        render->rc->realloc_buffer(renderable.index_buffer, &ib_res);
        renderable.index_buffer_capacity = updated_buffer_capacity;
    }
    else
    {
        render->my->update_buffer(renderable.index_buffer, 0, min_ib_capacity, data.indices.data());
    }

    renderable.data.index_count = data.indices.size();

    // Update data texture contents with new instances
    renderable.instance_data.clear();
    generate_text_instance_data(consumable_commands, renderable.instance_data);

    if (renderable.data.instance_data_texture_lazy.is_null())
    {
        my::TextureResource t_res;
        t_res.layout.type = my::TextureLayout::Type2D;
        t_res.layout.format = my::TextureFormat::RGBA32F;
        t_res.layout.width = TextDataTextureWidth;
        t_res.layout.height = TextDataTextureHeight;
        t_res.layout.depth = 1;
        t_res.layout.levels = 1;
        t_res.data = {};
        t_res.generate_mipmaps = false;
        t_res.allow_allocation_failure = true;

        renderable.data.instance_data_texture_lazy = render->rc->alloc(
            &t_res, monitoring::systems::DevTools,
            {{"contents"_ss, "debug text instance texture"_ss}});

        if (renderable.data.instance_data_texture_lazy.is_null())
        {
            return;
        }
    }

    const size_t instance_bytes = renderable.instance_data.size() * sizeof(TextInstanceData);
    assert(
        instance_bytes <= TextDataTextureWidth * TextDataTextureHeight * TextDataTexturePixelSize);

    std::span<const std::byte> instance_data(
        (std::byte*)renderable.instance_data.data(), instance_bytes);

    // Do the update in two blocks: one for the rows that are full and one for the last row
    static constexpr size_t row_bytes = TextDataTextureWidth * TextDataTexturePixelSize;
    const size_t full_rows = instance_bytes / row_bytes;

    render->my->update_texture(
        renderable.data.instance_data_texture_lazy, my::TextureFormat::RGBA32F, 0, 0, 0, 0,
        TextDataTextureWidth, full_rows, 1, instance_data.first(full_rows * row_bytes));

    const size_t last_row_bytes = instance_bytes % row_bytes;
    const size_t last_row_pixels = last_row_bytes / TextDataTexturePixelSize;

    render->my->update_texture(
        renderable.data.instance_data_texture_lazy, my::TextureFormat::RGBA32F, 0, 0, full_rows, 0,
        last_row_pixels, 1, 1, instance_data.subspan(full_rows * row_bytes, last_row_bytes));
}

void clean_text_renderable(TextRenderable& renderable, Render* render)
{
    render->rc->dealloc(renderable.vertex_buffer);
    render->rc->dealloc(renderable.index_buffer);
    render->rc->dealloc(renderable.data.vertex_input);
    render->rc->dealloc(renderable.data.instance_data_texture_lazy);
    render->rc->dealloc(renderable.data.instance_data_sampler);
}

void init_primitive_renderable(
    std::span<PrimitiveCommand> consumable_commands,
    my::PrimitiveType primitive_type,
    PrimitiveRenderable& renderable,
    DebugDrawSystem* dd,
    Render* render)
{
    auto vertex_data = generate_primitive_vertex_data(consumable_commands);

    my::BufferResource vb_res(my::BufferResource::BufferType::Vertex);
    vb_res.size = vertex_data.size() * sizeof(PrimitiveVertex);
    vb_res.usage = my::UsageHint::Dynamic;
    vb_res.data = vertex_data.data();

    my::ResourceHandle vertex_buffer = render->rc->alloc(&vb_res, monitoring::systems::DevTools);

    my::VertexInputStream streams[] = {
        {0, vertex_buffer, my::VertexFormat::Float32_3, 0, sizeof(PrimitiveVertex),
         my::VertexRate::PerVertex},
        {1, vertex_buffer, my::VertexFormat::Float32_3, sizeof(lm::vec3), sizeof(PrimitiveVertex),
         my::VertexRate::PerVertex},
        {2, vertex_buffer, my::VertexFormat::UInt8Norm_4, sizeof(lm::vec3) * 2,
         sizeof(PrimitiveVertex), my::VertexRate::PerVertex},
        {3, vertex_buffer, my::VertexFormat::UInt8, sizeof(lm::vec3) * 2 + sizeof(lm::ubvec4),
         sizeof(PrimitiveVertex), my::VertexRate::PerVertex},
    };

    my::VertexInputResource vi_res;
    vi_res.attrib_count = HRZ_ARRAY_COUNT(streams);
    vi_res.attribs = streams;

    my::ResourceHandle vertex_input = render->rc->alloc(&vi_res, monitoring::systems::DevTools);

    renderable.vertex_buffer = vertex_buffer;
    renderable.vertex_buffer_capacity = vb_res.size;
    renderable.data.vertex_count = vertex_data.size();
    renderable.data.vertex_input = vertex_input;
    renderable.data.shader = dd->primitive_shader;
    renderable.data.primitive_type = primitive_type;
}

void update_primitive_renderable(
    std::span<PrimitiveCommand> consumable_commands,
    PrimitiveRenderable& renderable,
    Render* render)
{
    HRZ_SCOPED_SAMPLE("debug drawing update primitive renderable");

    auto vertex_data = generate_primitive_vertex_data(consumable_commands);

    size_t min_buffer_capacity = vertex_data.size() * sizeof(PrimitiveVertex);
    if (renderable.vertex_buffer_capacity < min_buffer_capacity)
    {
        size_t updated_buffer_capacity =
            std::max(min_buffer_capacity, 2 * renderable.vertex_buffer_capacity);
        vertex_data.reserve(updated_buffer_capacity / sizeof(PrimitiveVertex));

        vertex_data.reserve(updated_buffer_capacity / sizeof(PrimitiveVertex));

        my::BufferResource vb_res(my::BufferResource::BufferType::Vertex);
        vb_res.size = updated_buffer_capacity;
        vb_res.usage = my::UsageHint::Dynamic;
        vb_res.data = vertex_data.data();

        render->rc->realloc_buffer(renderable.vertex_buffer, &vb_res);
        renderable.vertex_buffer_capacity = updated_buffer_capacity;
    }
    else
    {
        render->my->update_buffer(
            renderable.vertex_buffer, 0, min_buffer_capacity, vertex_data.data());
    }

    renderable.data.vertex_count = vertex_data.size();
}

void clean_primitive_renderable(PrimitiveRenderable& renderable, Render* render)
{
    render->rc->dealloc(renderable.vertex_buffer);
    render->rc->dealloc(renderable.data.vertex_input);
}

void init_render(DebugDrawSystem* dd, Render* render)
{
    // Primitive shader
    {
        static const my::IndexName attribs[] = {
            {0, "i_position_low"},
            {1, "i_position_high"},
            {2, "i_color"},
            {3, "i_coordinate_space"}};

        static const my::IndexName ubos[] = {{hrz::UboFrame, "Frame"}};

        static const char* outputs[] = {"o_color"};

        my::ShaderResource res{};
        res.name = hrz_shaders::DebugDraw_name;
        res.link_hint = my::ShaderLinkHint::FirstUseImmediate;
        res.vertex_source_len = hrz_shaders::DebugDraw_vert_len;
        res.vertex_source = hrz_shaders::DebugDraw_vert;
        res.fragment_source_len = hrz_shaders::DebugDraw_frag_len;
        res.fragment_source = hrz_shaders::DebugDraw_frag;
        res.attrib_count = HRZ_ARRAY_COUNT(attribs);
        res.attribs = attribs;
        res.uniform_block_count = HRZ_ARRAY_COUNT(ubos);
        res.uniform_blocks = ubos;
        res.sampler_count = 0;
        res.samplers = nullptr;
        res.output_count = HRZ_ARRAY_COUNT(outputs);
        res.outputs = outputs;
        res.initial_state.rasterization.cull_mode = my::RasterizationState::None;
        res.initial_state.depth.test = false;
        res.initial_state.color_blend.enable = true;
        res.initial_state.color_blend.color.src = my::ColorBlendState::SrcAlpha;
        res.initial_state.color_blend.color.dst = my::ColorBlendState::OneMinusSrcAlpha;

        dd->primitive_shader = render->rc->alloc(&res, hrz::monitoring::systems::DevTools);
    }

    // Text shader
    {
        static const my::IndexName attribs[] = {{0, "i_position"}};

        static const my::IndexName ubos[] = {{hrz::UboFrame, "Frame"}};

        static const my::IndexName samplers[] = {
            {SamplerTextInstanceData, "u_instance_data"},
            {SamplerTextFontAtlas, "u_font_atlas"}};

        static const char* outputs[] = {"o_color"};

        my::ShaderResource res{};
        res.name = hrz_shaders::DebugText_name;
        res.link_hint = my::ShaderLinkHint::FirstUseImmediate;
        res.vertex_source_len = hrz_shaders::DebugText_vert_len;
        res.vertex_source = hrz_shaders::DebugText_vert;
        res.fragment_source_len = hrz_shaders::DebugText_frag_len;
        res.fragment_source = hrz_shaders::DebugText_frag;
        res.attrib_count = HRZ_ARRAY_COUNT(attribs);
        res.attribs = attribs;
        res.uniform_block_count = HRZ_ARRAY_COUNT(ubos);
        res.uniform_blocks = ubos;
        res.sampler_count = HRZ_ARRAY_COUNT(samplers);
        res.samplers = samplers;
        res.output_count = HRZ_ARRAY_COUNT(outputs);
        res.outputs = outputs;
        res.initial_state.rasterization.cull_mode = my::RasterizationState::None;
        res.initial_state.color_blend.enable = true;
        res.initial_state.color_blend.color.src = my::ColorBlendState::SrcAlpha;
        res.initial_state.color_blend.color.dst = my::ColorBlendState::OneMinusSrcAlpha;
        res.initial_state.depth.test = false;
        res.initial_state.depth.write = false;

        dd->text_shader = render->rc->alloc(&res, hrz::monitoring::systems::DevTools);
    }

    // Renderables
    {
        init_primitive_renderable(
            g_line_commands, my::PrimitiveType::LineList, dd->lines_renderable, dd, render);
        init_primitive_renderable(
            g_point_commands, my::PrimitiveType::PointList, dd->points_renderable, dd, render);
        init_primitive_renderable(
            g_triangle_commands, my::PrimitiveType::TriangleList, dd->triangles_renderable, dd,
            render);
        init_text_renderable(g_text_commands, dd->text_renderable, dd, render);
    }

    // Font data
    {
        auto blob = hrz_res::get_data(hrz_res::Resources::DefaultFont);

        constexpr size_t char_count = 96;
        constexpr size_t texture_size = 512;
        constexpr size_t pixel_count = texture_size * texture_size;

        std::byte pixels[pixel_count];
        stbtt_bakedchar cdata[char_count];

        dd->font_data.min_char = 32;
        dd->font_data.char_count = char_count;

        stbtt_BakeFontBitmap(
            (const unsigned char*)blob.data(), 0, 24.0f, (unsigned char*)pixels, texture_size,
            texture_size, dd->font_data.min_char, char_count, cdata);

        dd->font_data.char_data.resize(char_count);
        for (size_t i = 0; i < char_count; i++)
        {
            dd->font_data.char_data[i] = cdata[i];
        }

        my::TextureResource t_res;
        t_res.generate_mipmaps = false;
        t_res.layout.depth = 1;
        t_res.layout.levels = 1;
        t_res.layout.type = my::TextureLayout::Type2D;
        t_res.layout.format = my::TextureFormat::R8;
        t_res.layout.width = texture_size;
        t_res.layout.height = texture_size;

        auto upload_data = std::span<const std::byte>{pixels, pixel_count};
        t_res.data = {&upload_data, 1};

        dd->font_data.atlas_texture = render->rc->alloc(
            &t_res, hrz::monitoring::systems::DevTools, {{"contents"_ss, "debug font atlas"_ss}});

        my::SamplerResource s_res;
        s_res.sampler.min_filter = my::SamplerParams::Filter::Linear;
        s_res.sampler.mag_filter = my::SamplerParams::Filter::Linear;
        s_res.sampler.wrap_x = my::SamplerParams::Wrap::Clamp;
        s_res.sampler.wrap_y = my::SamplerParams::Wrap::Clamp;
        s_res.sampler.wrap_z = my::SamplerParams::Wrap::Clamp;
        s_res.use_mipmaps = false;

        dd->font_data.atlas_sampler = render->rc->alloc(
            &s_res, hrz::monitoring::systems::DevTools, {{"contents"_ss, "debug font sampler"_ss}});
    }

    dd->display.init_render(render);

    dd->texture_renderable.init_render(render);
}

void deinit_render(DebugDrawSystem* dd, Render* render)
{
    render->rc->dealloc(dd->font_data.atlas_texture);
    render->rc->dealloc(dd->font_data.atlas_sampler);

    dd->display.deinit_render(render);
    dd->texture_renderable.deinit_render(render);
}
} // namespace

DebugDrawSystem* create_system()
{
    auto dd = new DebugDrawSystem();
    return dd;
}

void destroy_system(DebugDrawSystem* dd, Render* render)
{
    assert(dd && render);

    clean_primitive_renderable(dd->lines_renderable, render);
    clean_primitive_renderable(dd->points_renderable, render);
    clean_primitive_renderable(dd->triangles_renderable, render);
    clean_text_renderable(dd->text_renderable, render);

    deinit_render(dd, render);

    delete dd;
}

void initialize_rendering(DebugDrawSystem* dd, Render* render)
{
    assert(dd && render);
    init_render(dd, render);
}

RenderRequest work_gpu(DebugDrawSystem* dd, Render* render)
{
    assert(dd && render);
    HRZ_SCOPED_SAMPLE("debug drawing work gpu");

    RenderRequest render_request;

    bool has_commands = (!g_line_commands.empty()) || (!g_point_commands.empty())
        || (!g_triangle_commands.empty()) || (!g_text_commands.empty());

    if (has_commands || has_commands != dd->rendered_last_frame)
    {
        update_primitive_renderable(g_line_commands, dd->lines_renderable, render);
        update_primitive_renderable(g_point_commands, dd->points_renderable, render);
        update_primitive_renderable(g_triangle_commands, dd->triangles_renderable, render);
        update_text_renderable(g_text_commands, dd->text_renderable, dd->font_data, render);

        g_line_commands.clear();
        g_point_commands.clear();
        g_triangle_commands.clear();
        g_text_commands.clear();

        render_request.request_visual_render();
        dd->rendered_last_frame = has_commands;
    }
    else
    {
        dd->rendered_last_frame = false;
    }

    return render_request;
}

void draw(DebugDrawSystem* dd, Render* render)
{
    HRZ_SCOPED_SAMPLE("debug drawing draw");

    render->rd->collect_renderable(dd->lines_renderable);
    render->rd->collect_renderable(dd->points_renderable);
    render->rd->collect_renderable(dd->triangles_renderable);
    render->rd->collect_renderable(dd->text_renderable);

    if (g_texture.has_value() && !g_texture.value().is_null())
    {
        dd->texture_renderable.data.texture = g_texture.value();
        render->rd->collect_renderable(dd->texture_renderable);
        g_texture = std::nullopt;
    }
}

void draw_display(DebugDrawSystem* dd, my::RenderContext* r)
{
    dd->display.draw(r);
}

inline bool is_visible(Group group)
{
    return g_visibility_mask.test((size_t)group);
}

void polyline(std::span<const double> coords, const lm::vec4& color, Space space, Group group)
{
    if (!is_visible(group)) return;

    g_line_commands.push_back({});
    auto& command = g_line_commands.back();

    command.color = (lm::ubvec4)(color * 255.0f);
    command.coordinate_space = space;

    command.points.reserve(((coords.size() / 3) - 1) * 2);
    for (size_t i = 0; i < coords.size() - 3; i += 3)
    {
        command.points.push_back({coords[i], coords[i + 1], coords[i + 2]});
        command.points.push_back({coords[i + 3], coords[i + 4], coords[i + 5]});
    }
}

void points(std::span<const double> coords, const lm::vec4& color, Space space, Group group)
{
    if (!is_visible(group)) return;

    g_point_commands.push_back({});
    auto& command = g_point_commands.back();

    command.color = (lm::ubvec4)(color * 255.0f);
    command.coordinate_space = space;

    command.points.reserve(coords.size() / 3);
    for (size_t i = 0; i < coords.size(); i += 3)
    {
        command.points.push_back({coords[i], coords[i + 1], coords[i + 2]});
    }
}

void triangles(std::span<const double> coords, const lm::vec4& color, Space space, Group group)
{
    if (!is_visible(group)) return;

    g_triangle_commands.push_back({});
    auto& command = g_triangle_commands.back();

    command.color = (lm::ubvec4)(color * 255.0f);
    command.coordinate_space = space;

    command.points.reserve(((coords.size() / 3) / 3) * 3);
    for (size_t i = 0; i < coords.size() - 9; i += 3)
    {
        command.points.push_back({coords[i], coords[i + 1], coords[i + 2]});
        command.points.push_back({coords[i + 3], coords[i + 4], coords[i + 5]});
        command.points.push_back({coords[i + 6], coords[i + 7], coords[i + 8]});
    }
}

void texture(my::ResourceHandle texture)
{
    g_texture = {texture};
}

void text(
    std::string_view text,
    const lm::dvec3& position,
    const lm::vec4& color,
    TextAlign align,
    Space space,
    Group group)
{
    if (!is_visible(group)) return;

    g_text_commands.push_back({});
    auto& command = g_text_commands.back();

    command.text = text.data();
    command.position = position;
    command.color = color;
    command.align = align;
    command.coordinate_space = space;
}

const char* get_group_name(Group group)
{
    switch (group)
    {
#define HRZ_DEBUG_DRAW_GROUP(NAME) \
    case Group_##NAME: return #NAME;
        HRZ_DEBUG_DRAW_GROUPS
#undef HRZ_DEBUG_DRAW_GROUP
        default: return "Undefined";
    }
}

void dev_ui(DebugDrawSystem* dd, mu_Context* ctx, const char* window_name)
{
    assert(dd);

    if (mu_begin_window_ex(ctx, window_name, mu_rect(300, 200, 350, 370), MU_OPT_CLOSED))
    {
        static int layout = -1;
        mu_layout_row(ctx, 1, &layout, 0);

        if (mu_header(ctx, "Group visibility"))
        {
            for (size_t i = 0; i < GroupCount; i++)
            {
                int visible = is_visible((Group)i);

                mu_push_id(ctx, &i, sizeof(size_t));
                mu_checkbox(ctx, get_group_name((Group)i), &visible);
                mu_pop_id(ctx);

                g_visibility_mask.set(i, visible);
            }
        }

        if (mu_header(ctx, "Display"))
        {
            int* pixel_grid_enabled = (int*)&dd->display.pixel_grid_enabled;
            mu_checkbox(ctx, "Enable pixel grid", pixel_grid_enabled);

            int draw_flat_overlay_cascade = (int)get_flag(Flag::DebugDrawFlatOverlayCascades);
            if (mu_checkbox(ctx, "Draw flat overlay cascades", &draw_flat_overlay_cascade))
            {
                set_flag(Flag::DebugDrawFlatOverlayCascades, (bool)draw_flat_overlay_cascade);
            }

            int draw_heatmap_oob_range = (int)get_flag(Flag::DebugDrawHeatmapOobSampling);
            if (mu_checkbox(ctx, "Draw heatmap out-of-bounds sampling", &draw_heatmap_oob_range))
            {
                set_flag(Flag::DebugDrawHeatmapOobSampling, (bool)draw_heatmap_oob_range);
            }

            int freeze_3dtiles_culling = (int)get_flag(Flag::DebugFreeze3DTilesCulling);
            if (mu_checkbox(ctx, "Freeze 3D Tiles culling", &freeze_3dtiles_culling))
            {
                set_flag(Flag::DebugFreeze3DTilesCulling, (bool)freeze_3dtiles_culling);
            }

            int freeze_vector_tiles_culling = (int)get_flag(Flag::DebugFreezeVectorTilesCulling);
            if (mu_checkbox(ctx, "Freeze vector tiles culling", &freeze_vector_tiles_culling))
            {
                set_flag(Flag::DebugFreezeVectorTilesCulling, (bool)freeze_vector_tiles_culling);
            }

            int draw_vector_tile_bounds = (int)get_flag(Flag::DebugDrawVectorTileBounds);
            if (mu_checkbox(ctx, "Draw vector tile bounds", &draw_vector_tile_bounds))
            {
                set_flag(Flag::DebugDrawVectorTileBounds, (bool)draw_vector_tile_bounds);
            }

            int draw_horizon_occlusion_points =
                (int)get_flag(Flag::DebugDrawHorizonOcclusionPoints);
            if (mu_checkbox(ctx, "Draw horizon culling points", &draw_horizon_occlusion_points))
            {
                set_flag(
                    Flag::DebugDrawHorizonOcclusionPoints, (bool)draw_horizon_occlusion_points);
            }

            int draw_terrain_patch_bboxes = (int)get_flag(Flag::DebugDrawTerrainPatchBboxes);
            if (mu_checkbox(ctx, "Draw terrain patch bounding boxes", &draw_terrain_patch_bboxes))
            {
                set_flag(Flag::DebugDrawTerrainPatchBboxes, (bool)draw_terrain_patch_bboxes);
            }
        }

        mu_end_window(ctx);
    }
}

} // namespace debug_draw

void DebugDraw::polyline(std::span<const lm::dvec3> points) const
{
    dd::polyline({(const double*)points.data(), points.size() * 3}, color, space, group);
}

void DebugDraw::polyline(std::initializer_list<lm::dvec3> points) const
{
    dd::polyline({(const double*)points.begin(), points.size() * 3}, color, space, group);
}

void DebugDraw::polyline_geo(std::span<const GeoPosition3> points) const
{
    dd::polyline(
        {(const double*)points.data(), points.size() * 3}, color, dd::Space::LatLonAltRad, group);
}

void DebugDraw::polyline_geo(std::initializer_list<GeoPosition3> points) const
{
    dd::polyline(
        {(const double*)points.begin(), points.size() * 3}, color, dd::Space::LatLonAltRad, group);
}

void DebugDraw::points(std::span<const lm::dvec3> points) const
{
    dd::points({(const double*)points.data(), points.size() * 3}, color, space, group);
}

void DebugDraw::points(std::initializer_list<lm::dvec3> points) const
{
    dd::points({(const double*)points.begin(), points.size() * 3}, color, space, group);
}

void DebugDraw::points_geo(std::span<const GeoPosition3> points) const
{
    dd::points(
        {(const double*)points.data(), points.size() * 3}, color, dd::Space::LatLonAltRad, group);
}

void DebugDraw::points_geo(std::initializer_list<GeoPosition3> points) const
{
    dd::points(
        {(const double*)points.begin(), points.size() * 3}, color, dd::Space::LatLonAltRad, group);
}

void DebugDraw::triangles(std::span<const lm::dvec3> points) const
{
    dd::triangles({(const double*)points.data(), points.size() * 3}, color, space, group);
}

void DebugDraw::triangles(std::initializer_list<lm::dvec3> points) const
{
    dd::triangles({(const double*)points.begin(), points.size() * 3}, color, space, group);
}

void DebugDraw::triangles_geo(std::span<const GeoPosition3> points) const
{
    dd::triangles(
        {(const double*)points.data(), points.size() * 3}, color, dd::Space::LatLonAltRad, group);
}

void DebugDraw::triangles_geo(std::initializer_list<GeoPosition3> points) const
{
    dd::triangles(
        {(const double*)points.begin(), points.size() * 3}, color, dd::Space::LatLonAltRad, group);
}

void DebugDraw::wgs84_box(const hrz::GeoVolumeBounds& bounds) const
{
    const auto center_geo = bounds.center();
    auto enu = hrz::enu_to_ecef_rotation_matrix_for_geo(center_geo.lat, center_geo.lon);

    auto east = enu.x.xyz;
    auto north = enu.y.xyz;
    auto up = enu.z.xyz;

    const auto center_ecef = hrz::geo_to_ecef(center_geo);
    double x_he =
        lm::length(hrz::geo_to_ecef(bounds.corner(0)) - hrz::geo_to_ecef(bounds.corner(2))) / 2.0;
    double y_he =
        lm::length(hrz::geo_to_ecef(bounds.corner(0)) - hrz::geo_to_ecef(bounds.corner(1))) / 2.0;
    double z_he = (bounds.max_height - bounds.min_height) / 2.0;

#define CORNER(x, y, z) center_ecef + x* east* x_he + y* north* y_he + z* up* z_he

    polyline({CORNER(-1, -1, -1), CORNER(1, -1, -1)});
    polyline({CORNER(-1, -1, -1), CORNER(-1, 1, -1)});
    polyline({CORNER(-1, -1, -1), CORNER(-1, -1, 1)});

    polyline({CORNER(-1, 1, 1), CORNER(1, 1, 1)});
    polyline({CORNER(-1, 1, 1), CORNER(-1, -1, 1)});
    polyline({CORNER(-1, 1, 1), CORNER(-1, 1, -1)});

    polyline({CORNER(1, 1, -1), CORNER(-1, 1, -1)});
    polyline({CORNER(1, 1, -1), CORNER(1, -1, -1)});
    polyline({CORNER(1, 1, -1), CORNER(1, 1, 1)});

    polyline({CORNER(1, -1, 1), CORNER(-1, -1, 1)});
    polyline({CORNER(1, -1, 1), CORNER(1, 1, 1)});
    polyline({CORNER(1, -1, 1), CORNER(1, -1, -1)});

#undef CORNER
}

void DebugDraw::text(std::string_view text, const lm::dvec3& position) const
{
    dd::text(text, position, color, text_align, space, group);
}

void DebugDraw::point_text(std::string_view text, const lm::dvec3& position) const
{
    dd::points({&position.x, 3}, color, space, group);
    dd::text(text, position, color, text_align, space, group);
}

void DebugDraw::screen_text(std::string_view text, const lm::dvec2& position) const
{
    dd::text(text, position, color, text_align, dd::Space::Screen, group);
}

void DebugDraw::clip_text(std::string_view text, const lm::dvec2& position) const
{
    dd::text(text, position, color, text_align, dd::Space::Clip, group);
}

void DebugDraw::text_geo(std::string_view text, const GeoPosition3& geo) const
{
    const lm::dvec3 position = {geo.lat, geo.lon, geo.alt};
    dd::text(text, position, color, text_align, dd::Space::LatLonAltRad, group);
}

void DebugDraw::point_text_geo(std::string_view text, const hrz::GeoPosition3& geo) const
{
    const lm::dvec3 position = {geo.lat, geo.lon, geo.alt};
    dd::points({&position.x, 3}, color, dd::Space::LatLonAltRad, group);
    dd::text(text, position, color, text_align, dd::Space::LatLonAltRad, group);
}

} // namespace hrz
