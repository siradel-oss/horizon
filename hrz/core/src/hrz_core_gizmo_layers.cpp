#include "hrz_core_gizmo_layers.h"

#include "camera/hrz_core_camera_system.h"
#include "hrz_core_client_messages.h"
#include "hrz_core_events.h"
#include "hrz_core_gestures.h"
#include "hrz_core_grid.h"
#include "hrz_core_picking_id_allocator.h"
#include "hrz_core_render.h"
#include "hrz_core_shaders.h"

#include <hrz_common_geo.h>
#include <hrz_common_geometry.h>
#include <hrz_common_monitoring_defs.h>
#include <hrz_common_profiling.h>
#include <hrz_common_proto_geo.h>
#include <hrz_common_proto_maths.h>
#include <hrz_fnd_flat_hash_map.h>
#include <hrz_fnd_flat_hash_set.h>
#include <hrz_fnd_gen_object_pool.h>
#include <hrz_fnd_mem.h>
#include <hrz_protocol_path_builder.h>

#include <lin_maths.h>

#include <array>
#include <limits>
#include <optional>

namespace
{
enum
{
    UboGizmo = hrz::UboCustomStart,
    InputStreamVertex = 0,

    InputStreamUv = 0,
};

// The following values configure each component that composes the final gizmo. The values are
// expressed in [0, 1]. This is important as later computations are based on this fact.

// Configuration of the translation arrows.
static constexpr size_t ARROW_RESOLUTION = 8;
static constexpr float ARROW_BODY_LENGTH = 0.7f;
static constexpr float ARROW_BODY_RADIUS = 0.02f;
static constexpr float ARROW_CONE_LENGTH = 0.3f;
static constexpr float ARROW_CONE_RADIUS = 0.1f;

// Configuration of the translation square.
static constexpr lm::dvec3 TRANSLATION_SQUARE_OFFSET = lm::dvec3(0.5, 0.5, 0);
static constexpr float TRANSLATION_SQUARE_SIZE = 0.2f;
static constexpr float TRANSLATION_SQUARE_FADEOUT_THRESHOLD = 0.6f;

// Configuration of the camera plane circles.
static constexpr float RING_OUTLINE_SIZE = 0.1f;

// Configuration of the camera plane translation circle.
static constexpr float TRANSLATION_CAM_PLANE_RADIUS = 0.28f;
static constexpr float TRANSLATION_CAM_PLANE_OUTER_RADIUS =
    TRANSLATION_CAM_PLANE_RADIUS + RING_OUTLINE_SIZE * 0.5f;

// Configuration of the camera plane rotation circle.
static constexpr float ROTATION_CAM_PLANE_RADIUS = 1.5f;

// Configuration of rotation torus.
static constexpr size_t TORUS_RESOLUTION = 32;
static constexpr size_t TORUS_CIRCLE_RESOLUTION = 8;
static constexpr float ROTATION_TORUS_MAJOR_RADIUS = 1.0f + TRANSLATION_CAM_PLANE_OUTER_RADIUS;
static constexpr float ROTATION_TORUS_MINOR_RADIUS = ROTATION_TORUS_MAJOR_RADIUS * 0.025f;

// Configuration of the dot at the gizmo origin.
static constexpr float ORIGIN_RADIUS = 0.05f;
static constexpr float ORIGIN_OUTLINE_SIZE = 0.025f;

static constexpr float DEFAULT_FADEOUT_THRESHOLD = 0.9f;

static constexpr lm::vec4 RED(0.9, 0.19, 0.3, 1);
static constexpr lm::vec4 BLUE(0.19, 0.53, 0.89, 1);
static constexpr lm::vec4 GREEN(0.5, 0.79, 0, 1);
static constexpr lm::vec4 WHITE(0.9, 0.9, 0.9, 0.9);
static constexpr lm::vec4 BLACK(0, 0, 0, 1);
static constexpr lm::vec4 ORANGE(1, 0.5, 0.25, 1);
static constexpr lm::vec4 HIGHLIGHT(0.9, 0.8, 0.1, 1.0);

struct GizmoMeshUniformData
{
    lm::mat4 transform;
    lm::vec4 color;
    float alpha_fadeout{};
    uint32_t _padding[3]{};
};

HRZ_CHECK_UBO_SIZE(GizmoMeshUniformData);

struct GizmoCircleUniformData
{
    lm::vec4 circle_inner_color;
    lm::vec4 circle_outline_color;
    lm::vec3 offset;
    float circle_radius{};
    float circle_outline_size{};
    uint32_t _padding[3]{};
};

HRZ_CHECK_UBO_SIZE(GizmoCircleUniformData);

struct LineUniformData
{
    lm::vec4 color;
    lm::vec3 ecef_cc_pos;
    float extent{};
    lm::vec3 axis;
    float width_px{};
};

HRZ_CHECK_UBO_SIZE(LineUniformData);

// Remaps `screen_coords` expressed in the `view_info.view_size` space to the
// `view_info.viewport_size` space.
inline lm::vec2 remap_screen_coords(
    const lm::vec2& screen_coords,
    const hrz::CameraViewInfo& view_info)
{
    return lm::vec2(screen_coords * view_info.viewport.size / view_info.viewport.subview_size());
}

inline lm::vec2 ecef_to_pixel(const lm::dvec3& p, const hrz::CameraViewInfo& view_info)
{
    lm::dvec4 clip = view_info.pv * lm::dvec4(p, 1.0);
    lm::dvec2 ndc = 0.5 * (clip.xy / clip.w) + lm::vec2(0.5);
    return lm::vec2(ndc * view_info.viewport.size);
}

hrz::Ray pixel_to_ray(const lm::dvec2& pixel, const hrz::CameraViewInfo& view_info)
{
    lm::dmat4 inv_pv = lm::inverse(view_info.pv);
    const lm::dvec4 ndc(
        (pixel.x / view_info.viewport.size.x) * 2 - 1,
        (1 - (pixel.y / view_info.viewport.size.y)) * 2 - 1, -1, 1);
    lm::dvec4 world(inv_pv * ndc);
    world /= world.w;

    hrz::Ray ray;
    ray.o = view_info.cam.pos;
    ray.dir = lm::normalize(world.xyz - view_info.cam.pos);

    return ray;
}

std::pair<double, double> corrected_lines_intersection(
    const hrz::CameraViewInfo& view_info,
    const lm::dvec3& p0,
    const lm::dvec3& v0,
    const lm::vec2& mouse_pos)
{
    // Ref:
    // https://web.archive.org/web/20220517135747/https://ourmachinery.com/post/linear-algebra-shenanigans-gizmo-repair/
    //
    // Project the gizmo active axis into screen space. This gives us the 2D line
    // in screen space [0, 1] defined by a point sa and the direction (sb - sa) / |sb - sa|.
    // Find the closest point to the cursor on the 2D line.
    lm::dvec4 a = view_info.pv * lm::dvec4(p0, 1);
    lm::dvec4 b = view_info.pv * lm::dvec4(p0 + v0, 1);

    lm::dvec2 sa = {(a.x / a.w) * 0.5 + 0.5, 1 - ((a.y / a.w) * 0.5 + 0.5)};
    lm::dvec2 sb = {(b.x / b.w) * 0.5 + 0.5, 1 - ((b.y / b.w) * 0.5 + 0.5)};

    lm::dvec2 gizmo_screen_pos = sa;
    lm::dvec2 gizmo_screen_dir = lm::normalize(sb - sa);

    lm::dvec2 mouse_screen_pos(mouse_pos / view_info.viewport.size);
    lm::dvec2 proj_mouse_pos = gizmo_screen_pos
        + lm::dot(gizmo_screen_dir, mouse_screen_pos - gizmo_screen_pos) * gizmo_screen_dir;

    // Transform the projected point on the 2D axis into a 3D ray and find the closest point
    // between the line defined by this ray and the line defined by the active axis.
    hrz::Ray mouse_ray = pixel_to_ray(proj_mouse_pos * view_info.viewport.size, view_info);
    auto st = hrz::find_lines_closest_points(p0, v0, mouse_ray.o, mouse_ray.dir);

    return st;
}

void generate_arrow_geometry(std::vector<lm::vec3>& vertex_data, std::vector<uint32_t>& indices)
{
    // The arrow is seen as three circles:
    //  - the arrow body tail circle (cylinder)
    //  - the arrow body head circle (cylinder)
    //  - the arrow head circle (cone)
    // And three center points which are respectively centers:
    //  - the body tail circle center
    //  - the body head circle center
    //  - the head cone apex
    //
    // The geometry first stores the three center points then, vertices from
    // each circle are stored (vertices of a circle are contiguous).

    // Generate vertices on a unit circle.
    lm::vec2 circle_vertices[ARROW_RESOLUTION];
    for (size_t i = 0; i < ARROW_RESOLUTION; ++i)
    {
        float theta = (i / (float)ARROW_RESOLUTION) * 2 * lm::PIf;
        circle_vertices[i] = lm::vec2(std::cos(theta), std::sin(theta));
    }

    // Generate arrow geometry.
    vertex_data.resize(3 * (ARROW_RESOLUTION + 1));
    indices.reserve(18 * ARROW_RESOLUTION);

    vertex_data[0] = lm::vec3(0, 0, 0);
    vertex_data[1] = lm::vec3(0, 0, ARROW_BODY_LENGTH);
    vertex_data[2] = lm::vec3(0, 0, ARROW_BODY_LENGTH + ARROW_CONE_LENGTH);

    size_t offset_1 = 3;
    size_t offset_2 = offset_1 + ARROW_RESOLUTION;
    size_t offset_3 = offset_2 + ARROW_RESOLUTION;

    for (size_t i = 0; i < ARROW_RESOLUTION; ++i)
    {
        lm::vec2 c = circle_vertices[i];

        lm::vec2 p1 = ARROW_BODY_RADIUS * c;
        lm::vec2 p2 = ARROW_CONE_RADIUS * c;

        // Vertices are added by arrow "slices". For a given angle add the vertices to
        // every circle.
        vertex_data[offset_1 + i] = lm::vec3(p1, 0);
        vertex_data[offset_2 + i] = lm::vec3(p1, ARROW_BODY_LENGTH);
        vertex_data[offset_3 + i] = lm::vec3(p2, ARROW_BODY_LENGTH);

        // Though, to create triangles, indices can refer to vertices that haven't been added yet.
        uint32_t current_vertex_idx = i;
        uint32_t next_vertex_idx = (i + 1) % ARROW_RESOLUTION;

        // Arrow body (cylinder).
        {
            uint32_t a = offset_1 + current_vertex_idx;
            uint32_t b = offset_1 + next_vertex_idx;
            uint32_t c = offset_2 + current_vertex_idx;
            uint32_t d = offset_2 + next_vertex_idx;

            // Triangle for the circle at the arrow tail.
            indices.push_back(b);
            indices.push_back(a);
            indices.push_back(0);

            // Cylinder lateral surface.
            indices.push_back(b);
            indices.push_back(c);
            indices.push_back(a);
            indices.push_back(d);
            indices.push_back(c);
            indices.push_back(b);
        }

        // Arrow head (cone).
        {
            uint32_t a = offset_3 + current_vertex_idx;
            uint32_t b = offset_3 + next_vertex_idx;

            uint32_t c = offset_2 + current_vertex_idx;
            uint32_t d = offset_2 + next_vertex_idx;

            // Cone base.
            indices.push_back(b);
            indices.push_back(a);
            indices.push_back(c);

            indices.push_back(c);
            indices.push_back(d);
            indices.push_back(b);

            // Cone lateral surface.
            indices.push_back(a);
            indices.push_back(b);
            indices.push_back(2);
        }
    }
}

void generate_square_geometry(std::vector<lm::vec3>& vertex_data, std::vector<uint32_t>& indices)
{
    vertex_data.push_back(lm::vec3(0, 0, 0));
    vertex_data.push_back(lm::vec3(1, 0, 0));
    vertex_data.push_back(lm::vec3(1, 1, 0));
    vertex_data.push_back(lm::vec3(0, 1, 0));
    indices = {0, 1, 2, 0, 2, 3, 0, 2, 1, 0, 3, 2};
}

void generate_torus_geometry(std::vector<lm::vec3>& vertex_data, std::vector<uint32_t>& indices)
{
    vertex_data.reserve(TORUS_RESOLUTION * TORUS_CIRCLE_RESOLUTION);
    indices.reserve(TORUS_RESOLUTION * TORUS_CIRCLE_RESOLUTION * 6);

    static constexpr float MINOR_RADIUS = ROTATION_TORUS_MINOR_RADIUS / ROTATION_TORUS_MAJOR_RADIUS;

    // Generate all the points
    for (uint32_t j = 0; j < TORUS_RESOLUTION; j++)
    {
        float c_x = (float)std::cos((double)j * 2.0 * lm::PI / TORUS_RESOLUTION);
        float c_y = (float)std::sin((double)j * 2.0 * lm::PI / TORUS_RESOLUTION);
        lm::vec2 c(c_x, c_y);

        for (uint32_t i = 0; i < TORUS_CIRCLE_RESOLUTION; ++i)
        {
            float d_xy = (float)std::cos((double)i * 2.0 * lm::PI / TORUS_CIRCLE_RESOLUTION);
            float d_z = (float)std::sin((double)i * 2.0 * lm::PI / TORUS_CIRCLE_RESOLUTION);

            lm::vec2 p = c + c * d_xy * MINOR_RADIUS;
            lm::vec3 v(p, d_z * MINOR_RADIUS);

            vertex_data.push_back(v);
        }
    }

    // c0 and c1 are the base indices of the vertices of each minor circle
    // around the faces being generated.
    uint32_t c0 = TORUS_CIRCLE_RESOLUTION * (TORUS_RESOLUTION - 1);
    uint32_t c1 = 0;

    // Generate the faces
    for (uint32_t j = 0; j < TORUS_RESOLUTION; j++)
    {
        uint32_t index_offset_0 = TORUS_CIRCLE_RESOLUTION - 1;
        uint32_t index_offset_1 = 0;

        for (uint32_t i = 0; i < TORUS_CIRCLE_RESOLUTION; ++i)
        {
            indices.push_back(c0 + index_offset_0);
            indices.push_back(c1 + index_offset_0);
            indices.push_back(c1 + index_offset_1);

            indices.push_back(c0 + index_offset_0);
            indices.push_back(c1 + index_offset_1);
            indices.push_back(c0 + index_offset_1);

            index_offset_0 = index_offset_1;
            index_offset_1 += 1;
        }

        c0 = c1;
        c1 += TORUS_CIRCLE_RESOLUTION;
    }
}

struct LineParams
{
    lm::vec4 color;
    float width{};
    float extent{};
    uint32_t scene_views_bitset{};
    hrz_proto::UiSizeUnit extent_unit = hrz_proto::UiSizeUnit::UI_SIZE_IN_METERS;
};

LineParams from_proto(const hrz_proto::Line& line)
{
    LineParams params;
    params.color = hrz::to_lm(line.color());
    params.width = line.width();
    params.extent = line.extent();
    params.scene_views_bitset = line.scene_views().bits();
    params.extent_unit = line.extent_unit();
    return params;
}

class LineRenderable : public my::Renderer::Renderable
{
    struct Vertex
    {
        lm::vec2 uv;
    };

    struct RenderData
    {
        my::ResourceHandle shader;
        my::ResourceHandle vertex_input;
        my::ResourceHandle ubo;
    };

    RenderData _data;
    my::ResourceHandle _vertex_buffer;

    bool _should_render = false;
    lm::dvec3 _center;
    double _radius{};
    LineUniformData _uniform_data;

    static void render_callback(
        uint32_t render_type,
        my::RenderContext* r,
        my::ResourceBinder* rb,
        const void* user_data_raw,
        const void* raw_data)
    {
        const RenderData* data = (const RenderData*)raw_data;

        if (render_type != hrz::RenderVisual) return;

        auto batch = my::DrawBatchInfo(my::PrimitiveType::TriangleStrip, 4);

        rb->push_state();

        my::UboBinding ubo_bindings[] = {{UboGizmo, data->ubo, 0, sizeof(LineUniformData)}};
        rb->bind(HRZ_ARRAY_COUNT(ubo_bindings), ubo_bindings);

        auto state = rb->get_current_state();

        r->draw(
            batch, data->shader, data->vertex_input, state.ubo_count, state.ubos,
            state.texture_count, state.textures);

        rb->pop_state();
    }

    void collect_render_info(my::Renderer::Queue& queue, const my::Renderer::Culler& culler)
        const override
    {
        queue.enqueue(hrz::RenderUiBin, render_callback, &_data, lm::dvec3(0), 100'000'000'000);
    }

public:
    static void collect_shaders(hrz::GpuResourceContext* rc)
    {
        {
            static const my::IndexName attribs[] = {
                {InputStreamUv, "i_uv"},
            };

            static const my::IndexName ubos[] = {
                {hrz::UboFrame, "Frame"},
                {UboGizmo, "Line"},
            };

            static const char* outputs[] = {
                "o_color",
            };

            static const my::IndexName samplers[] = {
                {0, "u_scene_depth"},
                {1, "u_peel_depth"},
            };

            my::ShaderResource res{};
            res.name = hrz_shaders::Gizmo_line_name;
            res.vertex_source_len = hrz_shaders::Gizmo_line_vert_len;
            res.vertex_source = hrz_shaders::Gizmo_line_vert;
            res.fragment_source_len = hrz_shaders::Gizmo_line_frag_len;
            res.fragment_source = hrz_shaders::Gizmo_line_frag;
            res.uniform_block_count = HRZ_ARRAY_COUNT(ubos);
            res.uniform_blocks = ubos;
            res.output_count = HRZ_ARRAY_COUNT(outputs);
            res.outputs = outputs;
            res.attrib_count = HRZ_ARRAY_COUNT(attribs);
            res.attribs = attribs;
            res.sampler_count = HRZ_ARRAY_COUNT(samplers);
            res.samplers = samplers;

            res.initial_state.depth.test = true;
            hrz::render::initialize_ui_blending_params(&res.initial_state.color_blend);
            res.initial_state.rasterization.cull_mode = my::RasterizationState::CullMode::None;

            rc->alloc(&res, hrz::monitoring::systems::Gizmos);
        }
    }

    void initialize_rendering(hrz::Render* render)
    {
        _data.shader = render->rc->retrieve_shader(hrz_shaders::Gizmo_line_name);

        {
            Vertex vertex_data[4] =
                {{lm::vec2(0, 0)}, {lm::vec2(0, 1)}, {lm::vec2(1, 0)}, {lm::vec2(1, 1)}};

            my::BufferResource vb_res(my::BufferResource::BufferType::Vertex);
            vb_res.size = sizeof(vertex_data);
            vb_res.data = vertex_data;
            vb_res.usage = my::UsageHint::Static;

            my::ResourceHandle vb = render->rc->alloc(&vb_res, hrz::monitoring::systems::Gizmos);

            my::VertexInputStream streams[] = {
                {InputStreamUv, vb, my::VertexFormat::Float32_2, 0, sizeof(Vertex),
                 my::VertexRate::PerVertex},
            };

            my::VertexInputResource vi_res;
            vi_res.attrib_count = HRZ_ARRAY_COUNT(streams);
            vi_res.attribs = streams;

            my::ResourceHandle vi = render->rc->alloc(&vi_res, hrz::monitoring::systems::Gizmos);

            _vertex_buffer = vb;
            _data.vertex_input = vi;
        }

        // Init uniform buffer
        {
            _uniform_data.color = lm::vec4(1, 1, 1, 1);
            _uniform_data.width_px = 3.0f;
            _uniform_data.extent = 40'000.0f;

            my::BufferResource ub_res(my::BufferResource::BufferType::Uniform);
            ub_res.size = sizeof(_uniform_data);
            ub_res.data = &_uniform_data;
            ub_res.usage = my::UsageHint::Updatable;

            my::ResourceHandle ubo = render->rc->alloc(&ub_res, hrz::monitoring::systems::Gizmos);

            _data.ubo = ubo;
        }
    }

    void destroy(my::ResourceContext* rc)
    {
        // Deinit all
        rc->dealloc(_data.vertex_input);
        rc->dealloc(_data.ubo);
        rc->dealloc(_vertex_buffer);
    }

    void schedule_render(
        const LineParams& params,
        lm::dvec3 ecef_pos,
        lm::dvec3 ecef_pos_cc,
        lm::dvec3 axis,
        const hrz::CameraViewInfo& view_info)
    {
        _uniform_data.color = params.color;
        _uniform_data.width_px = params.width;
        _uniform_data.ecef_cc_pos = lm::vec3(ecef_pos_cc);
        _uniform_data.axis = lm::vec3(axis);

        switch (params.extent_unit)
        {
            case hrz_proto::UI_SIZE_IN_PIXELS:
                _uniform_data.extent = params.extent
                    * hrz::render::compute_logical_pixels_to_meters(ecef_pos, view_info);
                break;

            case hrz_proto::UI_SIZE_RELATIVE_TO_SCREEN:
                _uniform_data.extent = params.extent
                    * hrz::render::compute_device_pixels_to_meters(ecef_pos, view_info)
                    * std::min(view_info.viewport.size.x, view_info.viewport.size.y);
                break;

            case hrz_proto::UI_SIZE_IN_METERS:
            default: _uniform_data.extent = params.extent; break;
        }

        _center = ecef_pos;
        _radius = params.extent;

        _should_render = true;
    }

    void draw(hrz::Render* render)
    {
        if (_should_render)
        {
            render->my->update_buffer(_data.ubo, 0, sizeof(LineUniformData), &_uniform_data);
            render->rd->collect_renderable(*this);
            _should_render = false;
        }
    }
};

static float compute_alpha_fadeout(
    const lm::vec3& axis,
    const lm::vec3& view_dir,
    float threshold,
    bool fade_when_colinear)
{
    static const float cutoff = 0.1f;

    float dot = std::abs(lm::dot(axis, view_dir));
    float alpha = 1;

    if (fade_when_colinear)
    {
        alpha = dot > threshold ? 1 - ((dot - threshold) / (1 - threshold)) : 1;
    }
    else
    {
        alpha = dot < 1 - threshold ? dot / (1 - threshold) : 1;
    }

    // Ease-in.
    alpha = alpha * alpha;
    return alpha <= cutoff ? 0 : alpha;
}

template<typename UniformData>
class GizmoRenderable : public my::Renderer::Renderable
{
    static constexpr size_t MaxInstanceCount = 256;

    struct InstanceData
    {
        lm::dvec3 center;
        double radius{};
        size_t ubo_offset{};
        uint32_t scene_views_bitset{};
    };

    hrz::render::DoubleBufferedUniformBuffer<UniformData> _ubo;
    std::vector<InstanceData> _instances;

    my::ResourceHandle _vertex_buffer = my::ResourceHandle::null();
    my::ResourceHandle _vertex_input = my::ResourceHandle::null();
    my::ResourceHandle _index_buffer = my::ResourceHandle::null();
    my::ResourceHandle _shader = my::ResourceHandle::null();
    uint32_t _vertex_count = 0;

    struct RenderData
    {
        my::ResourceHandle vertex_input;
        my::ResourceHandle shader;
        my::ResourceHandle ubo;

        uint32_t vertex_count{};
        uint32_t scene_views_bitset{};

        size_t ubo_offset{};
    };

    static void render_callback(
        uint32_t render_type,
        my::RenderContext* r,
        my::ResourceBinder* rb,
        const void* user_data_raw,
        const void* raw_data)
    {
        const RenderData* data = (const RenderData*)raw_data;
        const auto* user_data = (const hrz::SceneViewRenderGraphUserData*)user_data_raw;

        if (render_type != hrz::RenderVisual) return;
        if (((1 << user_data->scene_view) & data->scene_views_bitset) == 0) return;

        auto batch = my::DrawBatchInfo(my::PrimitiveType::TriangleList, data->vertex_count)
                         .indexed(my::IndexType::UInt);

        rb->push_state();

        my::UboBinding ubo_bindings[] = {
            {UboGizmo, data->ubo, (uint32_t)data->ubo_offset, sizeof(UniformData)},
        };
        rb->bind(HRZ_ARRAY_COUNT(ubo_bindings), ubo_bindings);

        auto state = rb->get_current_state();

        r->draw(
            batch, data->shader, data->vertex_input, state.ubo_count, state.ubos,
            state.texture_count, state.textures);

        rb->pop_state();
    }

    void collect_render_info(my::Renderer::Queue& queue, const my::Renderer::Culler& culler)
        const override
    {
        for (const auto& instance : _instances)
        {
            if (culler.is_visible_in_any_view(instance.center, instance.radius, hrz::RenderUiBin))
            {
                RenderData data;
                data.vertex_input = _vertex_input;
                data.ubo = _ubo.get_for_gpu();
                data.shader = _shader;
                data.ubo_offset = instance.ubo_offset;
                data.vertex_count = _vertex_count;
                data.scene_views_bitset = instance.scene_views_bitset;

                queue.enqueue(
                    hrz::RenderUiBin, render_callback, &data, instance.center, instance.radius);
            }
        }
    }

public:
    GizmoRenderable() : _ubo(MaxInstanceCount) {}

    void initialize_rendering(
        my::Instance* my,
        hrz::GpuResourceContext* rc,
        my::ResourceHandle vertex_buffer,
        my::ResourceHandle index_buffer,
        my::ResourceHandle vertex_input,
        my::ResourceHandle shader,
        uint32_t vertex_count)
    {
        _vertex_buffer = vertex_buffer;
        _index_buffer = index_buffer;
        _vertex_input = vertex_input;
        _vertex_count = vertex_count;
        _shader = shader;

        hrz::Render render{};
        render.my = my;
        render.rc = rc;
        _ubo.initialize(
            &render, hrz::monitoring::systems::Gizmos, {{"contents"_ss, "gizmos ubo"_ss}});
    }

    void destroy(my::ResourceContext* rc)
    {
        rc->dealloc(_vertex_buffer);
        rc->dealloc(_vertex_input);
        rc->dealloc(_index_buffer);
        _ubo.destroy(rc);
    }

    void add_instance(
        lm::dvec3 center,
        double radius,
        uint32_t scene_views_bitset,
        const UniformData& data)
    {
        if (_instances.size() >= MaxInstanceCount) return;

        size_t instance_index = _instances.size();
        _ubo.set(instance_index, data);

        InstanceData instance_data;
        instance_data.center = center;
        instance_data.radius = radius;
        instance_data.scene_views_bitset = scene_views_bitset;
        instance_data.ubo_offset = _ubo.offset(instance_index);
        _instances.push_back(instance_data);
    }

    void draw(hrz::Render* render)
    {
        assert(_instances.size() < MaxInstanceCount);
        if (_instances.size() > 0)
        {
            _ubo.update(render->my);
            render->rd->collect_renderable(*this);
        }
    }

    void reset() { _instances.clear(); }
};

enum Axis
{
    East = 0,
    North,
    Up,
    CameraPlane,
};

enum class Action
{
    Idle,
    TranslateAxis,
    TranslatePlane,
    Rotate,
};

enum GizmoPartKind : uint16_t
{
    Origin = (1 << 0),
    AxisX = (1 << 1),
    AxisY = (1 << 2),
    AxisZ = (1 << 3),
    PlaneX = (1 << 4),
    PlaneY = (1 << 5),
    PlaneZ = (1 << 6),
    PlaneForward = (1 << 7),
    RingX = (1 << 8),
    RingY = (1 << 9),
    RingZ = (1 << 10),
    RingForward = (1 << 11),
};

constexpr uint32_t GIZMO_PART_COUNT = 12;

// Mask that describes which parts of a gizmo are active. Each bit set represents an active part of
// the gizmo. Any active part can be interacted with and is rendered to the screen. The bitfield
// composition is as follows:
// bit 0: the origin of the gizmo (little dot).
// bits 1-3: translation axis, north, east, up respectively.
// bits 4-7: translation planes, north, east, up, forward respectively.
// bits 8-11: rotation rings, north, east, up, forward respectively.
// bits 12-15: unused (*must* be 0).
using GizmoMask = uint16_t;

static constexpr GizmoMask GIZMO_AXIS_MASK =
    GizmoPartKind::AxisX | GizmoPartKind::AxisY | GizmoPartKind::AxisZ;
static constexpr GizmoMask GIZMO_PLANES_MASK =
    GizmoPartKind::PlaneX | GizmoPartKind::PlaneY | GizmoPartKind::PlaneZ;
static constexpr GizmoMask GIZMO_RINGS_MASK =
    GizmoPartKind::RingX | GizmoPartKind::RingY | GizmoPartKind::RingZ;

inline constexpr GizmoPartKind from_proto(hrz_proto::GizmoComponentType type)
{
    switch (type)
    {
        case hrz_proto::GIZMO_ORIGIN: return GizmoPartKind::Origin;
        case hrz_proto::GIZMO_TRANSLATION_AXIS_X: return GizmoPartKind::AxisX;
        case hrz_proto::GIZMO_TRANSLATION_AXIS_Y: return GizmoPartKind::AxisY;
        case hrz_proto::GIZMO_TRANSLATION_AXIS_Z: return GizmoPartKind::AxisZ;
        case hrz_proto::GIZMO_TRANSLATION_PLANE_X: return GizmoPartKind::PlaneX;
        case hrz_proto::GIZMO_TRANSLATION_PLANE_Y: return GizmoPartKind::PlaneY;
        case hrz_proto::GIZMO_TRANSLATION_PLANE_Z: return GizmoPartKind::PlaneZ;
        case hrz_proto::GIZMO_TRANSLATION_PLANE_CAMERA_PLANE: return GizmoPartKind::PlaneForward;
        case hrz_proto::GIZMO_ROTATION_RING_X: return GizmoPartKind::RingX;
        case hrz_proto::GIZMO_ROTATION_RING_Y: return GizmoPartKind::RingY;
        case hrz_proto::GIZMO_ROTATION_RING_Z: return GizmoPartKind::RingZ;
        case hrz_proto::GIZMO_ROTATION_RING_CAMERA_PLANE: return GizmoPartKind::RingForward;
        default: assert(!"Unhandled case"); break;
    }
    return (GizmoPartKind)0;
}

inline constexpr uint32_t to_index(GizmoPartKind kind)
{
    switch (kind)
    {
        case GizmoPartKind::Origin: return 0;
        case GizmoPartKind::AxisX: return 1;
        case GizmoPartKind::AxisY: return 2;
        case GizmoPartKind::AxisZ: return 3;
        case GizmoPartKind::PlaneX: return 4;
        case GizmoPartKind::PlaneY: return 5;
        case GizmoPartKind::PlaneZ: return 6;
        case GizmoPartKind::PlaneForward: return 7;
        case GizmoPartKind::RingX: return 8;
        case GizmoPartKind::RingY: return 9;
        case GizmoPartKind::RingZ: return 10;
        case GizmoPartKind::RingForward: return 11;
        default: assert(!"unhandled case"); break;
    }
    return 0;
}

static inline Axis part_to_axis(GizmoPartKind part)
{
    switch (part)
    {
        case GizmoPartKind::Origin: return (Axis)0;
        case GizmoPartKind::AxisX:
        case GizmoPartKind::PlaneX:
        case GizmoPartKind::RingX: return Axis::East;
        case GizmoPartKind::AxisY:
        case GizmoPartKind::PlaneY:
        case GizmoPartKind::RingY: return Axis::North;
        case GizmoPartKind::AxisZ:
        case GizmoPartKind::PlaneZ:
        case GizmoPartKind::RingZ: return Axis::Up;
        case GizmoPartKind::PlaneForward:
        case GizmoPartKind::RingForward: return Axis::CameraPlane;
        default: assert(!"Unhandled case"); break;
    }
    return (Axis)0;
}

static inline Action part_to_action(GizmoPartKind part)
{
    if (part & GIZMO_AXIS_MASK)
        return Action::TranslateAxis;
    else if (part & GIZMO_PLANES_MASK || part & GizmoPartKind::PlaneForward)
        return Action::TranslatePlane;
    else if (part & GIZMO_RINGS_MASK || part & GizmoPartKind::RingForward)
        return Action::Rotate;
    else
        return Action::Idle;
}

struct Part
{
    GizmoPartKind id = GizmoPartKind::Origin;
    Axis axis{};
    Action action{};

    Part() = default;

    explicit Part(GizmoPartKind id) : id(id), axis(part_to_axis(id)), action(part_to_action(id)) {}

    constexpr bool operator==(const Part& other) const { return id == other.id; }

    constexpr bool operator!=(const Part& other) const { return id != other.id; }
};

struct State
{
    Part part;

    lm::dvec3 active_axis;
    lm::dvec3 last_cursor_world_pos;
    lm::vec2 last_cursor_px_pos;
    lm::dvec3 world_hit_to_position_offset;
    bool action_end{};
};

struct HitState
{
    bool hit_occurred{};
    double best_hit_distance{};
    State next_state;

    bool improve_hit(double new_distance)
    {
        if (!hit_occurred || new_distance < best_hit_distance)
        {
            hit_occurred = true;
            best_hit_distance = new_distance;
            return true;
        }
        else
        {
            return false;
        }
    }
};

struct ArrowTrait
{
    using UniformData = GizmoMeshUniformData;

    struct ComponentParams
    {
        lm::dmat4 transform;
        lm::vec4 color;
        double offset{};
        float alpha_fadeout{};
        hrz_proto::GizmoReferenceFrame reference_frame{};
    };

    static hrz_proto::GizmoReferenceFrame get_reference_frame(const ComponentParams& params)
    {
        return params.reference_frame;
    }

    static void intersection(
        const Part& part,
        const ComponentParams& params,
        const hrz::Ray& ray,
        const hrz::CameraViewInfo& view_info,
        const lm::dmat4& global_display_transform,
        const lm::dvec3& position_ecef,
        const lm::vec2& mouse_pos,
        const lm::dvec3& bsphere_hit,
        const lm::dquat& local_rotation_matrix,
        double bbox_padding_meters,
        double scale_factor,
        HitState& hit_state)
    {
        lm::dvec3 hit;
        lm::dvec3 axis_dir = (global_display_transform * params.transform.col[2]).xyz;
        lm::dvec3 cylinder_base = position_ecef + params.offset * scale_factor * axis_dir;

        auto do_cylinder_intersection = [&](const lm::dvec3& base, double radius, double height)
        {
            if (hrz::ray_cylinder_intersection(ray, base, axis_dir, radius, height, &hit))
            {
                double hit_distance = lm::length2(hit - view_info.cam.pos);

                if (hit_state.improve_hit(hit_distance))
                {
                    auto st =
                        corrected_lines_intersection(view_info, position_ecef, axis_dir, mouse_pos);

                    hit_state.next_state.part = part;
                    hit_state.next_state.active_axis = axis_dir;
                    hit_state.next_state.world_hit_to_position_offset = st.first * axis_dir;
                }
            }
        };

        const double body_radius = (ARROW_BODY_RADIUS * scale_factor) + bbox_padding_meters;
        const double cone_radius = std::max(body_radius, ARROW_CONE_RADIUS * scale_factor);
        const double body_height = ARROW_BODY_LENGTH * scale_factor;
        const double cone_height = ARROW_CONE_LENGTH * scale_factor + bbox_padding_meters;
        const lm::dvec3 cone_base =
            cylinder_base + axis_dir * (body_height - 0.5 * bbox_padding_meters);

        do_cylinder_intersection(cylinder_base, body_radius, body_height);
        do_cylinder_intersection(cone_base, cone_radius, cone_height);
    }

    static UniformData get_uniform_data(
        const Part& part,
        const ComponentParams& params,
        const lm::dmat4& display_transform,
        const lm::dvec3& pos_cc,
        float alpha_fadeout,
        double scale_factor,
        const std::optional<Part>& highlighted_part)
    {
        lm::dmat4 full_transform =
            display_transform * params.transform * lm::translation(lm::dvec3(0, 0, params.offset));

        GizmoMeshUniformData data;
        data.color = (part == highlighted_part) ? HIGHLIGHT : params.color;
        data.color.a *= params.color.a;
        data.transform = lm::mat4(full_transform);
        data.alpha_fadeout = alpha_fadeout;

        return data;
    }

    static float get_alpha_fadeout(
        const ComponentParams& params,
        const lm::dmat4& rotation_transform,
        const lm::dmat4& display_transform)
    {
        lm::dmat4 full_transform =
            display_transform * params.transform * lm::translation(lm::dvec3(0, 0, params.offset));
        lm::dvec3 to = lm::normalize(full_transform.w.xyz);
        lm::dvec3 axis = lm::normalize((rotation_transform * params.transform.col[2]).xyz);
        return compute_alpha_fadeout(lm::vec3(axis), lm::vec3(to), DEFAULT_FADEOUT_THRESHOLD, true);
    }
};

struct PlaneTrait
{
    using UniformData = GizmoMeshUniformData;

    struct ComponentParams
    {
        lm::dmat4 transform;
        lm::dvec3 origin;
        lm::vec4 color;
        double size{};
        hrz_proto::GizmoReferenceFrame reference_frame{};
    };

    static hrz_proto::GizmoReferenceFrame get_reference_frame(const ComponentParams& params)
    {
        return params.reference_frame;
    }

    static void intersection(
        const Part& part,
        const ComponentParams& params,
        const hrz::Ray& ray,
        const hrz::CameraViewInfo& view_info,
        const lm::dmat4& global_display_transform,
        const lm::dvec3& position_ecef,
        const lm::vec2& mouse_pos,
        const lm::dvec3& bsphere_hit,
        const lm::dquat& local_rotation_matrix,
        double bbox_padding_meters,
        double scale_factor,
        HitState& hit_state)
    {
        lm::dvec3 hit;

        float bbox_size = params.size + 2 * bbox_padding_meters / scale_factor;
        lm::dvec3 offset = params.origin + lm::dvec3(lm::dvec2(params.size * 0.5));
        lm::dvec3 bbox_corner = lm::dvec3(lm::dvec2(-bbox_size * 0.5));
        lm::dvec3 bbox_origin = bbox_corner + offset;

        lm::dmat4 full_transform = global_display_transform * params.transform;
        lm::dvec3 origin_transformed =
            (full_transform * lm::dvec4(bbox_origin * scale_factor, 1)).xyz;

        if (hrz::ray_square_intersection(
                ray, position_ecef + origin_transformed, full_transform.col[0].xyz,
                full_transform.col[1].xyz, full_transform.col[2].xyz, bbox_size * scale_factor,
                &hit))
        {
            double hit_distance = lm::length2(hit - view_info.cam.pos);

            if (hit_state.improve_hit(hit_distance))
            {
                hit_state.next_state.part = part;
                hit_state.next_state.active_axis = lm::normalize(full_transform.col[2].xyz);
                hit_state.next_state.world_hit_to_position_offset = hit - position_ecef;
            }
        }
    }

    static UniformData get_uniform_data(
        const Part& part,
        const ComponentParams& params,
        const lm::dmat4& display_transform,
        const lm::dvec3& pos_cc,
        float alpha_fadeout,
        double scale_factor,
        const std::optional<Part>& highlighted_part)
    {
        lm::dmat4 full_transform = display_transform * params.transform
            * lm::translation(params.origin) * lm::scaling(params.size);

        GizmoMeshUniformData data;
        data.color = (part == highlighted_part) ? HIGHLIGHT : params.color;
        data.color.a *= params.color.a;
        data.transform = lm::mat4(full_transform);
        data.alpha_fadeout = alpha_fadeout;

        return data;
    }

    static float get_alpha_fadeout(
        const ComponentParams& params,
        const lm::dmat4& rotation_transform,
        const lm::dmat4& display_transform)
    {
        lm::dmat4 full_transform = display_transform * params.transform
            * lm::translation(params.origin) * lm::scaling(params.size);
        lm::dvec3 to = lm::normalize(full_transform.w.xyz);
        lm::dvec3 axis = lm::normalize((rotation_transform * params.transform).col[2].xyz);
        return compute_alpha_fadeout(
            lm::vec3(axis), lm::vec3(to), TRANSLATION_SQUARE_FADEOUT_THRESHOLD, false);
    }
};

struct TorusTrait
{
    using UniformData = GizmoMeshUniformData;

    struct ComponentParams
    {
        lm::dmat4 transform;
        lm::vec4 color;
        double major_radius{};
        hrz_proto::GizmoReferenceFrame reference_frame{};
    };

    static hrz_proto::GizmoReferenceFrame get_reference_frame(const ComponentParams& params)
    {
        return params.reference_frame;
    }

    static void intersection(
        const Part& part,
        const ComponentParams& params,
        const hrz::Ray& ray,
        const hrz::CameraViewInfo& view_info,
        const lm::dmat4& global_display_transform,
        const lm::dvec3& position_ecef,
        const lm::vec2& mouse_pos,
        const lm::dvec3& bsphere_hit,
        const lm::dquat& local_rotation,
        double bbox_padding_meters,
        double scale_factor,
        HitState& hit_state)
    {
        lm::dvec3 ecef_point_sphere_unit_vector = lm::normalize(bsphere_hit - position_ecef);
        lm::dmat4 local_rotation_matrix =
            params.reference_frame == hrz_proto::GizmoReferenceFrame::OBJECT_REFERENCE
            ? lm::rotation_normalized(local_rotation)
            : lm::dmat4::identity();
        lm::dmat4 ecef_to_enu_matrix =
            hrz::ecef_to_enu_rotation_matrix_for_geo(hrz::ecef_to_geo2(position_ecef));
        lm::dvec3 unit_sphere_hit_point_object_space =
            (lm::inverse(local_rotation_matrix) * ecef_to_enu_matrix
             * lm::dvec4(ecef_point_sphere_unit_vector, 1.0))
                .xyz;
        lm::dvec3 rotate_sphere_hit_point_world_space = ecef_point_sphere_unit_vector;

        lm::dmat4 full_transform = global_display_transform * params.transform;
        lm::dvec3 rot_axis = lm::normalize(full_transform.col[2].xyz);

        lm::dvec3 cam_to_center = lm::normalize(view_info.cam.pos - position_ecef);
        double dot_axis = std::abs(lm::dot(cam_to_center, rot_axis));

        // We increase the size of the minor torus a bit to make it easier to pick.
        double larger_minor_radius = ROTATION_TORUS_MINOR_RADIUS * 2.0;
        double inner_radius =
            (params.major_radius - larger_minor_radius) * scale_factor - bbox_padding_meters;
        double outer_radius =
            (params.major_radius + larger_minor_radius) * scale_factor + bbox_padding_meters;

        // test on plane if view is perpendicularish to the corresponding circle.
        if (dot_axis > 0.2)
        {
            lm::dvec3 hit;

            if (hrz::ray_ring_intersection(
                    ray, position_ecef, rot_axis, inner_radius, outer_radius, &hit))
            {
                double hit_distance = lm::length2(hit - view_info.cam.pos);
                if (hit_state.improve_hit(hit_distance))
                {
                    hit_state.next_state.part = part;
                    hit_state.next_state.last_cursor_world_pos = hit;
                    hit_state.next_state.active_axis = rot_axis;
                }
            }
        }
        else
        {
            if (std::abs(lm::dot(params.transform.col[2].xyz, unit_sphere_hit_point_object_space))
                < 0.05)
            {
                double hit_distance =
                    lm::length2(rotate_sphere_hit_point_world_space - view_info.cam.pos);
                if (hit_state.improve_hit(hit_distance))
                {
                    hit_state.next_state.part = part;
                    hit_state.next_state.last_cursor_world_pos =
                        rotate_sphere_hit_point_world_space;
                    hit_state.next_state.active_axis = rot_axis;
                }
            }
        }
    }

    static UniformData get_uniform_data(
        const Part& part,
        const ComponentParams& params,
        const lm::dmat4& display_transform,
        const lm::dvec3& pos_cc,
        float alpha_fadeout,
        double scale_factor,
        const std::optional<Part>& highlighted_part)
    {
        lm::dmat4 full_transform =
            display_transform * lm::scaling(params.major_radius) * params.transform;

        GizmoMeshUniformData data;
        data.color = (part == highlighted_part) ? HIGHLIGHT : params.color;
        data.color.a *= params.color.a;
        data.transform = lm::mat4(full_transform);
        data.alpha_fadeout = alpha_fadeout;

        return data;
    }

    static float get_alpha_fadeout(
        const ComponentParams& params,
        const lm::dmat4& rotation_transform,
        const lm::dmat4& display_transform)
    {
        lm::dmat4 full_transform =
            display_transform * lm::scaling(params.major_radius) * params.transform;
        lm::dvec3 to = lm::normalize(full_transform.w.xyz);
        lm::dvec3 axis = lm::normalize((rotation_transform * params.transform).col[2].xyz);
        return compute_alpha_fadeout(
            lm::vec3(axis), lm::vec3(to), DEFAULT_FADEOUT_THRESHOLD, false);
    }
};

struct CircleTrait
{
    using UniformData = GizmoCircleUniformData;

    struct ComponentParams
    {
        lm::vec4 inner_color;
        lm::vec4 outline_color;
        double outline_size{};
        double radius{};
    };

    static hrz_proto::GizmoReferenceFrame get_reference_frame(const ComponentParams& params)
    {
        return hrz_proto::GizmoReferenceFrame::WORLD_REFERENCE;
    }

    static void intersection(
        const Part& part,
        const ComponentParams& params,
        const hrz::Ray& ray,
        const hrz::CameraViewInfo& view_info,
        const lm::dmat4& global_display_transform,
        const lm::dvec3& position_ecef,
        const lm::vec2& mouse_pos,
        const lm::dvec3& bsphere_hit,
        const lm::dquat& local_rotation_matrix,
        double bbox_padding_meters,
        double scale_factor,
        HitState& hit_state)
    {
        const double inner_radius =
            (params.radius - params.outline_size * 0.5) * scale_factor - bbox_padding_meters;
        const double outer_radius =
            (params.radius + params.outline_size * 0.5) * scale_factor + bbox_padding_meters;

        lm::dvec3 hit;

        if (hrz::ray_ring_intersection(
                ray, position_ecef, view_info.cam.forward(), inner_radius, outer_radius, &hit))
        {
            double hit_distance = lm::length2(hit - view_info.cam.pos);

            if (hit_state.improve_hit(hit_distance))
            {
                lm::vec2 hit_px = ecef_to_pixel(hit, view_info);
                hit_px.y = view_info.viewport.size.y - hit_px.y;

                hit_state.next_state.part = part;
                hit_state.next_state.world_hit_to_position_offset = hit - position_ecef;
                hit_state.next_state.last_cursor_px_pos = lm::vec2(hit_px);
                hit_state.next_state.active_axis = -view_info.cam.forward();
            }
        }
    }

    static UniformData get_uniform_data(
        const Part& part,
        const ComponentParams& params,
        const lm::dmat4& display_transform,
        const lm::dvec3& pos_cc,
        float alpha_fadeout,
        double scale_factor,
        const std::optional<Part>& highlighted_part)
    {
        UniformData data;
        data.offset = lm::vec3(pos_cc);

        data.circle_outline_size = (float)(params.outline_size * scale_factor);
        data.circle_radius = (float)(params.radius * scale_factor);

        data.circle_inner_color = (part == highlighted_part) ? HIGHLIGHT : params.inner_color;
        data.circle_inner_color.a *= params.inner_color.a;

        data.circle_outline_color = (part == highlighted_part) ? HIGHLIGHT : params.outline_color;
        data.circle_outline_color.a *= params.outline_color.a;

        return data;
    }

    static float get_alpha_fadeout(
        const ComponentParams& params,
        const lm::dmat4& rotation_transform,
        const lm::dmat4& display_transform)
    {
        return 1.0f;
    }
};

template<typename Traits>
struct GizmoComponentTemplate
{
    using UniformData = typename Traits::UniformData;
    using ComponentParams = typename Traits::ComponentParams;

    Part part;
    ComponentParams params;

    float alpha_fadeout = 1;

    GizmoComponentTemplate() = default;

    explicit GizmoComponentTemplate(GizmoPartKind kind) : part(kind) {}

    inline void intersection(
        const hrz::Ray& ray,
        const hrz::CameraViewInfo& view_info,
        const lm::dmat4& global_display_transform,
        const lm::dvec3& position_ecef,
        const lm::vec2& mouse_pos,
        const lm::dvec3& bsphere_hit,
        const lm::dquat& local_rotation_matrix,
        double bbox_padding_meters,
        double scale_factor,
        HitState& hit_state) const
    {
        if (alpha_fadeout != 0)
        {
            Traits::intersection(
                part, params, ray, view_info, global_display_transform, position_ecef, mouse_pos,
                bsphere_hit, local_rotation_matrix, bbox_padding_meters, scale_factor, hit_state);
        }
    }

    inline UniformData get_uniform_data(
        const lm::dmat4& display_transform,
        const lm::dvec3& pos_cc,
        double scale_factor,
        const std::optional<Part>& highlighted_part) const
    {
        return Traits::get_uniform_data(
            part, params, display_transform, pos_cc, alpha_fadeout, scale_factor, highlighted_part);
    }

    inline void update_alpha_fadeout(
        const lm::dmat4& rotation_transform,
        const lm::dmat4& display_transform)
    {
        alpha_fadeout = Traits::get_alpha_fadeout(params, rotation_transform, display_transform);
    }

    inline hrz_proto::GizmoReferenceFrame get_reference_frame() const
    {
        return Traits::get_reference_frame(params);
    }
};

class Gizmo
{
    using GizmoComponent = std::variant<
        GizmoComponentTemplate<ArrowTrait>,
        GizmoComponentTemplate<PlaneTrait>,
        GizmoComponentTemplate<TorusTrait>,
        GizmoComponentTemplate<CircleTrait>>;

    std::array<GizmoComponent, GIZMO_PART_COUNT> _components;

    State _state;
    State _candidate_state;

    uint64_t _id{};
    hrz::GeoPosition3 _position; // Geographic position in lat,lon,alt.
    lm::dvec3 _position_ecef;    // This is the position authority, not _position
    lm::dvec3 _last_idle_position;
    lm::dquat _local_rotation; // This is the rotation authority.

    GizmoMask _mask{};
    double _size{};
    hrz_proto::UiSizeUnit _size_unit{};
    double _bbox_padding{};
    uint32_t _scene_views_bitset = 0;
    bool _visible{};

    bool _received_control_move{};

    // Scene view index where the action was initiated. Empty when idling.
    std::optional<hrz_proto::SceneViewIndex> _active_view_index;
    lm::vec2 _mouse_pos = {0, 0};
    std::optional<hrz::gestures::GestureId> _active_gesture_id;

    // Action-Axis ID of the element to highlight when the cursor is above a gizmo part.
    std::optional<Part> _hovered_part;
    std::optional<Part> _highlighted_part;

    hrz::grid::GridParams _grid_params;
    LineParams _line_params;

    void _add_translation_grid(
        hrz::grid::Grid& grid_renderable,
        const hrz::CameraViewInfo& view_info,
        lm::dvec3 ecef_pos,
        const lm::dvec3& ref_ecef_pos,
        double scale_factor,
        uint32_t scene_views_bitset,
        const lm::dmat4& last_idle_transform)
    {
        lm::dvec3 axis_x, axis_y, axis_z;
        switch (_state.part.axis)
        {
            case Axis::East:
            {
                axis_x = last_idle_transform.y.xyz;
                axis_y = last_idle_transform.z.xyz;
                axis_z = last_idle_transform.x.xyz;
                break;
            }
            case Axis::North:
            {
                axis_x = last_idle_transform.x.xyz;
                axis_y = last_idle_transform.z.xyz;
                axis_z = last_idle_transform.y.xyz;
                break;
            }
            case Axis::Up:
            {
                axis_x = last_idle_transform.x.xyz;
                axis_y = last_idle_transform.y.xyz;
                axis_z = last_idle_transform.z.xyz;
                break;
            }
            case Axis::CameraPlane:
            {
                axis_x = view_info.cam.right();
                axis_y = view_info.cam.up();
                axis_z = view_info.cam.forward();
                break;
            }
            default: assert(false && "Unhandled case"); break;
        }

        // Offset the grid along the z-axis to avoid z-fighting with the gizmo.
        ecef_pos += axis_z * scale_factor * 0.001;

        // Offset the grid along the other axes to keep the lines aligned with those of the grid at
        // the beginning of the translation.
        lm::dvec3 ecef_offset = ecef_pos - ref_ecef_pos;
        lm::dvec2 offset = {lm::dot(ecef_offset, axis_x), lm::dot(ecef_offset, axis_y)};

        lm::dvec3 ecef_cc_pos = ecef_pos - view_info.cam.pos;
        grid_renderable.schedule_draw(
            _grid_params, ecef_pos, ecef_cc_pos, lm::vec3(axis_x), lm::vec3(axis_y),
            lm::vec2(offset), view_info, scene_views_bitset & _grid_params.scene_views_bitset);
    }

    void _add_translation_line(
        LineRenderable& line_renderable,
        const hrz::CameraViewInfo& view_info,
        lm::dvec3 ecef_pos,
        uint32_t scene_views_bitset,
        const lm::dmat4& last_idle_transform)
    {
        lm::dvec3 axis;

        uint32_t axis_id = _state.part.axis;
        switch (axis_id)
        {
            case Axis::East:
            {
                axis = last_idle_transform.x.xyz;
                break;
            }
            case Axis::North:
            {
                axis = last_idle_transform.y.xyz;
                break;
            }
            case Axis::Up:
            {
                axis = last_idle_transform.z.xyz;
                break;
            }
            default: assert(false && "Unhandled case"); break;
        }

        lm::dvec3 ecef_cc_pos = ecef_pos - view_info.cam.pos;
        line_renderable.schedule_render(_line_params, ecef_pos, ecef_cc_pos, axis, view_info);
    }

    bool _handle_platform_event(
        const hrz::platform::Event& event,
        bool is_in_viewport,
        hrz_proto::SceneViewIndex view_index,
        const hrz::CameraViewInfo& view_info)
    {
        if (!_visible) return false;

        bool is_scene_view_enabled = ((_scene_views_bitset >> (unsigned)view_index) & 0x1) != 0;

        switch (event.kind)
        {
            case hrz::platform::Event::Kind::MouseButtonDown:
            {
                if (_state.part.action == Action::Idle && is_scene_view_enabled && is_in_viewport
                    && event.mouse_button.button == hrz::platform::Event::MouseButton::Left)
                {
                    if (_candidate_state.part.action != Action::Idle)
                    {
                        _active_view_index = view_index;
                        _state = _candidate_state;
                        return true;
                    }
                }

                return false;
            }
            case hrz::platform::Event::Kind::MouseButtonUp: [[fallthrough]];
            case hrz::platform::Event::Kind::MouseLeave:
            {
                if (!_active_gesture_id.has_value())
                {
                    if (_state.part.action != Action::Idle)
                    {
                        _state.action_end = true;
                    }

                    _state.part = {};
                    _active_view_index = std::nullopt;
                }
                return false;
            }
            case hrz::platform::Event::Kind::MouseMove:
            {
                lm::vec2 mouse = remap_screen_coords(
                    lm::vec2(event.mouse_move.x, event.mouse_move.y), view_info);

                if (is_scene_view_enabled && is_in_viewport && _state.part.action == Action::Idle)
                {
                    State next_state;

                    if (intersect(view_info, mouse, &next_state))
                    {
                        _candidate_state = next_state;
                        _hovered_part = next_state.part;
                    }
                    else
                    {
                        _candidate_state = {};
                        _hovered_part = std::nullopt;
                    }
                }

                if (_state.part.action != Action::Idle && _active_view_index == view_index
                    && !_active_gesture_id.has_value())
                {
                    _mouse_pos = mouse;
                    _received_control_move = true;
                }

                return false;
            }
            default: break;
        }

        return false;
    }

    bool _handle_gesture_event(
        const hrz::gestures::Event& event,
        bool is_in_viewport,
        hrz_proto::SceneViewIndex view_index,
        const hrz::CameraViewInfo& view_info)
    {
        bool is_scene_view_enabled = ((_scene_views_bitset >> (unsigned)view_index) & 0x1) != 0;

        switch (event.kind)
        {
            case hrz::gestures::Event::Kind::New: break;
            case hrz::gestures::Event::Kind::Qualification:
            {
                if (event.finger_count() == hrz::gestures::FingerCount::One)
                {
                    const auto& single_finger_gesture = event.single_finger_gesture();

                    if (_state.part.action == Action::Idle && is_scene_view_enabled
                        && is_in_viewport
                        && single_finger_gesture.type
                            == hrz::gestures::SingleFingerGesture::Type::Drag)
                    {
                        assert(!_active_gesture_id.has_value());

                        _mouse_pos = remap_screen_coords(single_finger_gesture.position, view_info);
                        State next_state;
                        if (intersect(
                                view_info, single_finger_gesture.initial_position, &next_state))
                        {
                            _active_view_index = view_index;
                            _active_gesture_id = {single_finger_gesture.id};
                            _state = next_state;
                            return true;
                        }
                    }
                }

                break;
            }
            case hrz::gestures::Event::Kind::Move:
            {
                if (event.finger_count() == hrz::gestures::FingerCount::One)
                {
                    const auto& single_finger_gesture = event.single_finger_gesture();

                    if (_state.part.action != Action::Idle && _active_view_index == view_index
                        && _active_gesture_id == single_finger_gesture.id)
                    {
                        _mouse_pos = remap_screen_coords(single_finger_gesture.position, view_info);
                        _received_control_move = true;
                        return true;
                    }
                }

                break;
            }
            case hrz::gestures::Event::Kind::End:
            {
                if (event.finger_count() == hrz::gestures::FingerCount::One)
                {
                    const auto& single_finger_gesture = event.single_finger_gesture();

                    if (single_finger_gesture.id == _active_gesture_id)
                    {
                        if (_state.part.action != Action::Idle)
                        {
                            _state.action_end = true;
                        }

                        _active_view_index = std::nullopt;
                        _active_gesture_id = std::nullopt;
                    }

                    return false;
                }

                break;
            }
            default: break;
        }

        return false;
    }

public:
    Gizmo()
    {
        {
            auto c = GizmoComponentTemplate<CircleTrait>(GizmoPartKind::Origin);
            c.params.outline_size = ORIGIN_OUTLINE_SIZE;
            c.params.radius = ORIGIN_RADIUS;
            c.params.inner_color = ORANGE;
            c.params.outline_color = BLACK;
            _components[to_index(GizmoPartKind::Origin)] = c;
        }

        lm::dmat4 east_transform = lm::rotation(lm::dvec3(0, 1, 0), lm::radians(90.0));
        lm::dmat4 north_transform = lm::rotation(lm::dvec3(1, 0, 0), lm::radians(-90.0));

        {
            auto c = GizmoComponentTemplate<ArrowTrait>(GizmoPartKind::AxisX);
            c.params.offset = TRANSLATION_CAM_PLANE_OUTER_RADIUS;
            c.params.color = RED;
            c.params.transform = east_transform;
            _components[to_index(GizmoPartKind::AxisX)] = c;
        }
        {
            auto c = GizmoComponentTemplate<ArrowTrait>(GizmoPartKind::AxisY);
            c.params.offset = TRANSLATION_CAM_PLANE_OUTER_RADIUS;
            c.params.color = GREEN;
            c.params.transform = north_transform;
            _components[to_index(GizmoPartKind::AxisY)] = c;
        }
        {
            auto c = GizmoComponentTemplate<ArrowTrait>(GizmoPartKind::AxisZ);
            c.params.offset = TRANSLATION_CAM_PLANE_OUTER_RADIUS;
            c.params.color = BLUE;
            c.params.transform = lm::dmat4::identity();
            _components[to_index(GizmoPartKind::AxisZ)] = c;
        }

        east_transform = lm::rotation(lm::dvec3(0, 1, 0), lm::radians(-90.0));
        north_transform = lm::rotation(lm::dvec3(1, 0, 0), lm::radians(90.0));

        {
            auto c = GizmoComponentTemplate<PlaneTrait>(GizmoPartKind::PlaneX);
            c.params.origin = TRANSLATION_SQUARE_OFFSET;
            c.params.size = TRANSLATION_SQUARE_SIZE;
            c.params.color = RED;
            c.params.transform = east_transform;
            _components[to_index(GizmoPartKind::PlaneX)] = c;
        }
        {
            auto c = GizmoComponentTemplate<PlaneTrait>(GizmoPartKind::PlaneY);
            c.params.origin = TRANSLATION_SQUARE_OFFSET;
            c.params.size = TRANSLATION_SQUARE_SIZE;
            c.params.color = GREEN;
            c.params.transform = north_transform;
            _components[to_index(GizmoPartKind::PlaneY)] = c;
        }
        {
            auto c = GizmoComponentTemplate<PlaneTrait>(GizmoPartKind::PlaneZ);
            c.params.origin = TRANSLATION_SQUARE_OFFSET;
            c.params.size = TRANSLATION_SQUARE_SIZE;
            c.params.color = BLUE;
            c.params.transform = lm::dmat4::identity();
            _components[to_index(GizmoPartKind::PlaneZ)] = c;
        }
        {
            auto c = GizmoComponentTemplate<CircleTrait>(GizmoPartKind::PlaneForward);
            c.params.outline_size = RING_OUTLINE_SIZE;
            c.params.radius = TRANSLATION_CAM_PLANE_RADIUS;
            c.params.inner_color = lm::vec4(WHITE.rgb, 0);
            c.params.outline_color = WHITE;
            _components[to_index(GizmoPartKind::PlaneForward)] = c;
        }

        {
            auto c = GizmoComponentTemplate<TorusTrait>(GizmoPartKind::RingX);
            c.params.major_radius = ROTATION_TORUS_MAJOR_RADIUS;
            c.params.color = RED;
            c.params.transform = east_transform;
            _components[to_index(GizmoPartKind::RingX)] = c;
        }
        {
            auto c = GizmoComponentTemplate<TorusTrait>(GizmoPartKind::RingY);
            c.params.major_radius = ROTATION_TORUS_MAJOR_RADIUS;
            c.params.color = GREEN;
            c.params.transform = north_transform;
            _components[to_index(GizmoPartKind::RingY)] = c;
        }
        {
            auto c = GizmoComponentTemplate<TorusTrait>(GizmoPartKind::RingZ);
            c.params.major_radius = ROTATION_TORUS_MAJOR_RADIUS;
            c.params.color = BLUE;
            c.params.transform = lm::dmat4::identity();
            _components[to_index(GizmoPartKind::RingZ)] = c;
        }
        {
            auto c = GizmoComponentTemplate<CircleTrait>(GizmoPartKind::RingForward);
            c.params.outline_size = RING_OUTLINE_SIZE;
            c.params.radius = ROTATION_CAM_PLANE_RADIUS;
            c.params.inner_color = lm::vec4(WHITE.rgb, 0);
            c.params.outline_color = WHITE;
            _components[to_index(GizmoPartKind::RingForward)] = c;
        }
    }

    bool handle_event(
        const hrz::ViewportEvent& event,
        hrz_proto::SceneViewIndex view_index,
        const hrz::CameraViewInfo& view_info)
    {
        switch (event.kind)
        {
            case hrz::Event::Kind::Platform:
                return _handle_platform_event(
                    event.platform, event.is_in_viewport, view_index, view_info);
            case hrz::Event::Kind::Gesture:
                return _handle_gesture_event(
                    event.gesture, event.is_in_viewport, view_index, view_info);
            default: break;
        }

        return false;
    }

    double get_size_in_meters(const hrz::CameraViewInfo& view_info)
    {
        switch (_size_unit)
        {
            case hrz_proto::UiSizeUnit::UI_SIZE_IN_PIXELS:
                return hrz::render::compute_logical_pixels_to_meters(_position_ecef, view_info)
                    * _size / 2.0; // Size is the diameter

            case hrz_proto::UiSizeUnit::UI_SIZE_RELATIVE_TO_SCREEN:
                return hrz::render::compute_device_pixels_to_meters(_position_ecef, view_info)
                    * _size * std::min(view_info.viewport.size.x, view_info.viewport.size.y)
                    / 2.0; // Size is the diameter

            case hrz_proto::UiSizeUnit::UI_SIZE_IN_METERS:
            default: return _size;
        }
    }

    lm::dmat4 get_reference_frame_matrix(hrz_proto::GizmoReferenceFrame reference_frame) const
    {
        return reference_frame == hrz_proto::GizmoReferenceFrame::OBJECT_REFERENCE
            ? lm::rotation_normalized(_local_rotation)
            : lm::dmat4::identity();
    }

    bool intersect(const hrz::CameraViewInfo& view_info, const lm::vec2& pixel, State* state)
    {
        HRZ_SCOPED_SAMPLE("gizmo intersect");

        // Calculate a scale factor so that the gizmo remains at constant size no matter the
        // distance to the camera.
        double scale_factor = get_size_in_meters(view_info);
        double bbox_padding_meters =
            hrz::render::compute_logical_pixels_to_meters(_position_ecef, view_info)
            * _bbox_padding;

        // First perform a coarse intersection test with the component bounding sphere.
        // If the coarse test succeeds then finer intersection tests are performed for
        // each axis depending on the component type.

        hrz::Ray ray = pixel_to_ray(lm::dvec2(pixel), view_info);
        hrz::BSphere<double> bsphere = {
            _position_ecef, scale_factor * ROTATION_TORUS_MAJOR_RADIUS + bbox_padding_meters};

        lm::dvec3 hit;
        HitState hit_state;
        hit_state.hit_occurred = false;
        hit_state.best_hit_distance = std::numeric_limits<double>::infinity();

        const auto& enu_to_ecef = hrz::enu_to_ecef_rotation_matrix_for_geo(_position.latlon());

        if (hrz::ray_sphere_intersection(ray, bsphere, &hit))
        {
            for (const auto& c : _components)
            {
                std::visit(
                    [&](const auto& element)
                    {
                        if (!(_mask & element.part.id)) return;
                        if (element.part.id == GizmoPartKind::Origin) return;

                        lm::dmat4 global_display_transform =
                            enu_to_ecef * get_reference_frame_matrix(element.get_reference_frame());
                        element.intersection(
                            ray, view_info, global_display_transform, _position_ecef, pixel, hit,
                            _local_rotation, bbox_padding_meters, scale_factor, hit_state);
                    },
                    c);
            }
        }
        else if (_mask & GizmoPartKind::RingForward)
        {
            // The exterior ring for near plane rotations can be outside of the bounding sphere.

            uint32_t index = to_index(GizmoPartKind::RingForward);
            const auto& element = std::get<GizmoComponentTemplate<CircleTrait>>(_components[index]);

            lm::dmat4 global_display_transform =
                enu_to_ecef * get_reference_frame_matrix(element.get_reference_frame());

            element.intersection(
                ray, view_info, global_display_transform, _position_ecef, pixel, lm::dvec3(0),
                _local_rotation, bbox_padding_meters, scale_factor, hit_state);
        }

        if (hit_state.hit_occurred)
        {
            *state = std::move(hit_state.next_state);
            return true;
        }
        else
        {
            return false;
        }
    }

    void send_message(hrz::ClientMessageQueue* mq, bool is_final) const
    {
        hrz_proto::GizmoUpdateMessage message;
        message.set_id(_id);
        message.mutable_geo_pos()->set_latitude(lm::degrees(_position.lat));
        message.mutable_geo_pos()->set_longitude(lm::degrees(_position.lon));
        message.mutable_geo_pos()->set_altitude(_position.alt);
        message.mutable_rotation()->set_x(_local_rotation.x);
        message.mutable_rotation()->set_y(_local_rotation.y);
        message.mutable_rotation()->set_z(_local_rotation.z);
        message.mutable_rotation()->set_w(_local_rotation.w);
        message.set_action_end(is_final);
        hrz::client_message_queue::enqueue_gizmo_update_message(mq, std::move(message));
    }

    hrz::CameraViewInfo find_cam_view_info(
        std::span<const hrz::RenderViewInfo> views_infos,
        hrz_proto::SceneViewIndex view)
    {
        for (const auto& view_info : views_infos)
        {
            if (view_info.view == view)
            {
                return view_info.cam_view_info;
            }
        }

        assert(!"No view found?");
        return {};
    }

    hrz::RenderRequest work(
        hrz::ClientMessageQueue* mq,
        hrz::SceneModel* scene_model,
        uint64_t layer_id,
        hrz_proto::SceneViewIndex main_view_index,
        std::span<const hrz::RenderViewInfo> views_infos)
    {
        HRZ_SCOPED_SAMPLE("gizmo work");

        hrz::RenderRequest render_request;

        if (_mask == 0 || !_visible)
        {
            return render_request;
        }

        const hrz::CameraViewInfo view_info =
            find_cam_view_info(views_infos, _active_view_index.value_or(main_view_index));

        std::optional<Part> highlighted_part =
            _state.part.action != Action::Idle ? _state.part : _hovered_part;
        if (_highlighted_part != highlighted_part)
        {
            _highlighted_part = highlighted_part;
            render_request.request_visual_render();
        }

        if (_state.action_end)
        {
            send_message(mq, true);

            // Update the scene model when action has ended.
            hrz::SceneModelAccessor accessor(scene_model);
            hrz_proto::LayerHandle handle;
            handle.set_opaque(layer_id);
            hrz_proto::GizmoLayerPathBuilder<hrz::SceneModelAccessor> builder(accessor, handle);

            hrz_proto::GizmoLayer layer = builder.clone().get();

            layer.mutable_position()->set_latitude(lm::degrees(_position.lat));
            layer.mutable_position()->set_longitude(lm::degrees(_position.lon));
            layer.mutable_position()->set_altitude(_position.alt);
            layer.mutable_rotation()->set_x(_local_rotation.x);
            layer.mutable_rotation()->set_y(_local_rotation.y);
            layer.mutable_rotation()->set_z(_local_rotation.z);
            layer.mutable_rotation()->set_w(_local_rotation.w);

            builder.set(layer);

            _state.action_end = false;

            _last_idle_position = _position_ecef;

            render_request.request_visual_render();
        }

        bool has_changed = false;

        if (_received_control_move)
        {
            if (_state.part.action == Action::TranslateAxis)
            {
                assert(_mask & GIZMO_AXIS_MASK);

                auto st = corrected_lines_intersection(
                    view_info, _position_ecef, _state.active_axis, _mouse_pos);

                _position_ecef = _position_ecef + st.first * _state.active_axis
                    - _state.world_hit_to_position_offset;
                has_changed = true;
            }
            else if (_state.part.action == Action::TranslatePlane)
            {
                assert(_mask & GIZMO_PLANES_MASK || _mask & GizmoPartKind::PlaneForward);

                hrz::Ray ray = pixel_to_ray(lm::dvec2(_mouse_pos), view_info);

                lm::dvec3 hit;
                if (hrz::ray_plane_intersection(ray, _position_ecef, _state.active_axis, &hit))
                {
                    _position_ecef = hit - _state.world_hit_to_position_offset;
                    has_changed = true;
                }
            }
            else if (_state.part.action == Action::Rotate)
            {
                assert(_mask & GIZMO_RINGS_MASK || _mask & GizmoPartKind::RingForward);

                double angle = 0;

                if (_state.part.axis != Axis::CameraPlane)
                {
                    hrz::Ray ray = pixel_to_ray(lm::dvec2(_mouse_pos), view_info);
                    lm::dvec3 hit;
                    if (hrz::ray_plane_intersection(ray, _position_ecef, _state.active_axis, &hit))
                    {
                        lm::dvec3 v0 = lm::normalize(_state.last_cursor_world_pos - _position_ecef);
                        lm::dvec3 v1 = lm::normalize(hit - _position_ecef);

                        double d = lm::dot(v0, v1);
                        if (d < -0.9999)
                        {
                            angle = lm::PI;
                        }
                        else if (d <= 1.0)
                        {
                            angle = std::acos(d);
                            lm::dvec3 sv = lm::cross(v0, v1);
                            if (lm::dot(sv, _state.active_axis) < 0)
                            {
                                angle = 2 * lm::PI - angle;
                            }
                        }

                        _state.last_cursor_world_pos = hit;
                    }
                }
                else
                {
                    lm::vec2 center_px = ecef_to_pixel(_position_ecef, view_info);
                    // mouse pos origin is top left so we need no invert center pixel y coord
                    center_px.y = view_info.viewport.size.y - center_px.y;

                    lm::vec2 v0 = lm::normalize(lm::vec2(_state.last_cursor_px_pos) - center_px);
                    lm::vec2 v1 = lm::normalize(_mouse_pos - center_px);

                    angle = std::atan2(v0.y, v0.x) - std::atan2(v1.y, v1.x);
                    _state.last_cursor_px_pos = _mouse_pos;
                }

                if (angle != 0)
                {
                    lm::dmat4 global_to_local_matrix =
                        hrz::ecef_to_enu_rotation_matrix_for_geo(_position.lat, _position.lon);
                    lm::dvec3 local_axis =
                        (global_to_local_matrix * lm::dvec4(_state.active_axis, 1)).xyz;

                    _local_rotation = lm::axis_angle(local_axis, angle) * _local_rotation;
                    has_changed = true;
                }
            }
        }

        _position = hrz::ecef_to_geo3(_position_ecef);

        if (has_changed)
        {
            send_message(mq, false);
            render_request.request_visual_render();
        }

        _received_control_move = false;

        return render_request;
    }

    void draw(
        hrz::Render* render,
        std::span<const hrz::RenderViewInfo> views_info,
        GizmoRenderable<GizmoCircleUniformData>& circle_renderable,
        GizmoRenderable<GizmoMeshUniformData>& torus_renderable,
        GizmoRenderable<GizmoMeshUniformData>& square_renderable,
        GizmoRenderable<GizmoMeshUniformData>& arrow_renderable,
        LineRenderable& line_renderable,
        hrz::grid::Grid& grid_renderable)
    {
        HRZ_SCOPED_SAMPLE("gizmo draw");

        if (_mask == 0 || !_visible) return;

        for (const auto& view : views_info)
        {
            uint32_t scene_view_bitset = (1u << (int)view.view);
            if ((_scene_views_bitset & scene_view_bitset) == 0) return;

            const hrz::CameraViewInfo& view_info = view.cam_view_info;

            // Update gizmo size depending on its distance to the camera.
            double scale_factor = get_size_in_meters(view_info);
            lm::dvec3 position_offset_to_camera = _position_ecef - view_info.cam.pos;

            const auto& translation = lm::translation(position_offset_to_camera);
            const auto& scaling = lm::scaling(scale_factor);
            const auto& partial_rotation =
                hrz::enu_to_ecef_rotation_matrix_for_geo(_position.latlon());
            lm::dmat4 partial_display_transform = translation * partial_rotation * scaling;

            for (auto& c : _components)
            {
                std::visit(
                    [&](auto& element)
                    {
                        if (!(_mask & element.part.id)) return;

                        const auto& reference_frame_transform =
                            get_reference_frame_matrix(element.get_reference_frame());
                        lm::dmat4 display_transform =
                            partial_display_transform * reference_frame_transform;
                        lm::dmat4 global_display_transform =
                            partial_rotation * reference_frame_transform;

                        element.update_alpha_fadeout(global_display_transform, display_transform);

                        using T = std::decay_t<decltype(element)>;
                        if constexpr (std::is_same_v<T, GizmoComponentTemplate<ArrowTrait>>)
                        {
                            GizmoMeshUniformData data = element.get_uniform_data(
                                display_transform, position_offset_to_camera, scale_factor,
                                _highlighted_part);
                            arrow_renderable.add_instance(
                                _position_ecef, scale_factor, scene_view_bitset, data);

                            if (_state.part.action == Action::TranslateAxis
                                && _state.part.id == element.part.id)
                            {
                                _add_translation_line(
                                    line_renderable, view_info, _position_ecef, scene_view_bitset,
                                    global_display_transform);
                            }
                        }
                        else if constexpr (std::is_same_v<T, GizmoComponentTemplate<PlaneTrait>>)
                        {
                            GizmoMeshUniformData data = element.get_uniform_data(
                                display_transform, position_offset_to_camera, scale_factor,
                                _highlighted_part);
                            square_renderable.add_instance(
                                _position_ecef, scale_factor, scene_view_bitset, data);

                            if (_state.part.action == Action::TranslatePlane
                                && _state.part.id == element.part.id)
                            {
                                _add_translation_grid(
                                    grid_renderable, view_info, _position_ecef, _last_idle_position,
                                    scale_factor, scene_view_bitset, global_display_transform);
                            }
                        }
                        else if constexpr (std::is_same_v<T, GizmoComponentTemplate<TorusTrait>>)
                        {
                            GizmoMeshUniformData data = element.get_uniform_data(
                                display_transform, position_offset_to_camera, scale_factor,
                                _highlighted_part);
                            torus_renderable.add_instance(
                                _position_ecef, scale_factor, scene_view_bitset, data);
                        }
                        else if constexpr (std::is_same_v<T, GizmoComponentTemplate<CircleTrait>>)
                        {
                            GizmoCircleUniformData data = element.get_uniform_data(
                                display_transform, position_offset_to_camera, scale_factor,
                                _highlighted_part);
                            circle_renderable.add_instance(
                                _position_ecef, scale_factor, scene_view_bitset, data);

                            if (_state.part.action == Action::TranslatePlane
                                && _state.part.id == element.part.id)
                            {
                                _add_translation_grid(
                                    grid_renderable, view_info, _position_ecef, _last_idle_position,
                                    scale_factor, scene_view_bitset, global_display_transform);
                            }
                        }
                        else
                        {
                            static_assert(hrz::always_false<T>, "Unhandled gizmo component type");
                        }
                    },
                    c);
            }
        }
    }

    void update_from_model(const hrz_proto::GizmoLayer& model)
    {
        _id = model.id();
        _position = hrz::from_proto(model.position());
        _position_ecef = hrz::geo_to_ecef(_position);
        _local_rotation = lm::dquat(hrz::to_lm(model.rotation()));
        _scene_views_bitset = model.scene_views().bits();
        _size = model.size();
        _size_unit = model.size_unit();
        _bbox_padding = model.bbox_padding();
        _visible = model.visible();
        _grid_params = hrz::from_proto(model.grid());
        _line_params = from_proto(model.line());
        _last_idle_position = _position_ecef;

        _mask = 0;
        for (const auto& component_proto : model.components())
        {
            auto kind = from_proto(component_proto.type());
            auto& component = _components[to_index(kind)];

            if (GIZMO_AXIS_MASK & kind)
            {
                auto& c = std::get<GizmoComponentTemplate<ArrowTrait>>(component);
                c.params.reference_frame = component_proto.reference_frame();
            }
            else if (GIZMO_PLANES_MASK & kind)
            {
                auto& c = std::get<GizmoComponentTemplate<PlaneTrait>>(component);
                c.params.reference_frame = component_proto.reference_frame();
            }
            else if (GIZMO_RINGS_MASK & kind)
            {
                auto& c = std::get<GizmoComponentTemplate<TorusTrait>>(component);
                c.params.reference_frame = component_proto.reference_frame();
            }

            _mask |= kind;
        }

        double arrow_offset =
            _mask & GizmoPartKind::PlaneForward ? TRANSLATION_CAM_PLANE_OUTER_RADIUS : 0;
        for (int i = 0; i < 3; ++i)
        {
            uint32_t index = to_index((GizmoPartKind)(GizmoPartKind::AxisX << i));
            auto& c = std::get<GizmoComponentTemplate<ArrowTrait>>(_components[index]);
            c.params.offset = arrow_offset;
        }
    }
};

struct Layer
{
    uint64_t global_layer_id;
    Gizmo gizmo;
};
} // anonymous namespace

namespace hrz
{
struct GizmoLayerSystem
{
    using IndexPool = GenIndexPool<uint64_t, 32, 32>;
    using LayerPool = GenObjectPool<Layer, IndexPool, 64>;

    LayerPool layer_pool;
    hrz::flat_hash_map<uint64_t, uint64_t> layer_ids_to_handles;

    hrz::flat_hash_set<uint64_t> unregistered_layers;
    hrz::flat_hash_set<uint64_t> updated_layers;

    my::ResourceHandle gizmo_mesh_shader = my::ResourceHandle::null();
    my::ResourceHandle gizmo_circle_shader = my::ResourceHandle::null();

    GizmoRenderable<GizmoCircleUniformData> circle_renderable;
    GizmoRenderable<GizmoMeshUniformData> torus_renderable;
    GizmoRenderable<GizmoMeshUniformData> square_renderable;
    GizmoRenderable<GizmoMeshUniformData> arrow_renderable;
    LineRenderable line_renderable;
    grid::Grid grid_renderable;
};

namespace gizmo_layers
{
namespace
{
RenderRequest _unregister_layers(GizmoLayerSystem* system, SceneModel* scene_model)
{
    RenderRequest render_request;

    for (auto layer_id : system->unregistered_layers)
    {
        const auto it = system->layer_ids_to_handles.find(layer_id);
        if (it != std::end(system->layer_ids_to_handles))
        {
            hrz_proto::PathRoot root;
            root.mutable_gizmo_layer()->set_opaque(layer_id);
            scene_model::unregister_element(scene_model, root);

            const auto layer_handle = it->second;
            system->layer_pool.release(layer_handle);

            render_request.request_visual_render();
            system->layer_ids_to_handles.erase(it);
        }
    }
    system->unregistered_layers.clear();

    return render_request;
}
} // anonymous namespace

GizmoLayerSystem* create_system()
{
    return new GizmoLayerSystem();
}

void destroy_system(GizmoLayerSystem* system, Render* render, SceneModel* scene_model)
{
    assert(system);
    assert(scene_model);
    for (const auto& it : system->layer_ids_to_handles)
    {
        unregister_layer(system, it.first);
    }
    _unregister_layers(system, scene_model);

    system->grid_renderable.deinit_rendering(render);
    system->line_renderable.destroy(render->rc);
    system->circle_renderable.destroy(render->my);
    system->torus_renderable.destroy(render->my);
    system->square_renderable.destroy(render->my);
    system->arrow_renderable.destroy(render->my);

    delete system;
}

void initialize_rendering(GizmoLayerSystem* system, Render* render)
{
    assert(system);
    assert(render);

    system->gizmo_mesh_shader = render->rc->retrieve_shader(hrz_shaders::Gizmo_mesh_name);
    system->gizmo_circle_shader = render->rc->retrieve_shader(hrz_shaders::Gizmo_circle_name);

    {
        std::vector<lm::vec3> vertex_data;
        std::vector<uint32_t> indices;
        generate_torus_geometry(vertex_data, indices);

        my::BufferResource vb_res(my::BufferResource::BufferType::Vertex);
        vb_res.size = vertex_data.size() * sizeof(lm::vec3);
        vb_res.usage = my::UsageHint::Static;
        vb_res.data = (void*)&vertex_data[0];

        my::ResourceHandle vertex_buffer = render->rc->alloc(&vb_res, monitoring::systems::Gizmos);

        my::BufferResource ib_res(my::BufferResource::BufferType::Index);
        ib_res.size = indices.size() * sizeof(uint32_t);
        ib_res.usage = my::UsageHint::Static;
        ib_res.data = (void*)&indices[0];

        my::ResourceHandle index_buffer = render->rc->alloc(&ib_res, monitoring::systems::Gizmos);

        my::VertexInputStream streams[] = {
            {InputStreamVertex, vertex_buffer, my::VertexFormat::Float32_3, 0, 12,
             my::VertexRate::PerVertex},
        };

        my::VertexInputResource vi_res;
        vi_res.indices = index_buffer;
        vi_res.attrib_count = HRZ_ARRAY_COUNT(streams);
        vi_res.attribs = streams;

        my::ResourceHandle vertex_input = render->rc->alloc(&vi_res, monitoring::systems::Gizmos);
        uint32_t vertex_count = (uint32_t)indices.size();

        system->torus_renderable.initialize_rendering(
            render->my, render->rc, vertex_buffer, index_buffer, vertex_input,
            system->gizmo_mesh_shader, vertex_count);
    }

    {
        std::vector<lm::vec3> vertex_data;
        std::vector<uint32_t> indices;
        generate_square_geometry(vertex_data, indices);

        my::BufferResource vb_res(my::BufferResource::BufferType::Vertex);
        vb_res.size = vertex_data.size() * sizeof(lm::vec3);
        vb_res.usage = my::UsageHint::Static;
        vb_res.data = (void*)&vertex_data[0];

        my::ResourceHandle vertex_buffer = render->rc->alloc(&vb_res, monitoring::systems::Gizmos);

        my::BufferResource ib_res(my::BufferResource::BufferType::Index);
        ib_res.size = indices.size() * sizeof(uint32_t);
        ib_res.usage = my::UsageHint::Static;
        ib_res.data = (void*)&indices[0];

        my::ResourceHandle index_buffer = render->rc->alloc(&ib_res, monitoring::systems::Gizmos);

        my::VertexInputStream streams[] = {
            {InputStreamVertex, vertex_buffer, my::VertexFormat::Float32_3, 0, 12,
             my::VertexRate::PerVertex},
        };

        my::VertexInputResource vi_res;
        vi_res.indices = index_buffer;
        vi_res.attrib_count = HRZ_ARRAY_COUNT(streams);
        vi_res.attribs = streams;

        my::ResourceHandle vertex_input = render->rc->alloc(&vi_res, monitoring::systems::Gizmos);
        uint32_t vertex_count = (uint32_t)indices.size();

        system->square_renderable.initialize_rendering(
            render->my, render->rc, vertex_buffer, index_buffer, vertex_input,
            system->gizmo_mesh_shader, vertex_count);
    }

    {
        std::vector<lm::vec3> vertex_data;
        std::vector<uint32_t> indices;
        generate_arrow_geometry(vertex_data, indices);

        my::BufferResource vb_res(my::BufferResource::BufferType::Vertex);
        vb_res.size = vertex_data.size() * sizeof(lm::vec3);
        vb_res.usage = my::UsageHint::Static;
        vb_res.data = (void*)&vertex_data[0];

        my::ResourceHandle vertex_buffer = render->rc->alloc(&vb_res, monitoring::systems::Gizmos);

        my::BufferResource ib_res(my::BufferResource::BufferType::Index);
        ib_res.size = indices.size() * sizeof(uint32_t);
        ib_res.usage = my::UsageHint::Static;
        ib_res.data = (void*)&indices[0];

        my::ResourceHandle index_buffer = render->rc->alloc(&ib_res, monitoring::systems::Gizmos);

        my::VertexInputStream streams[] = {
            {InputStreamVertex, vertex_buffer, my::VertexFormat::Float32_3, 0, 12,
             my::VertexRate::PerVertex},
        };

        my::VertexInputResource vi_res;
        vi_res.indices = index_buffer;
        vi_res.attrib_count = HRZ_ARRAY_COUNT(streams);
        vi_res.attribs = streams;

        my::ResourceHandle vertex_input = render->rc->alloc(&vi_res, monitoring::systems::Gizmos);
        uint32_t vertex_count = (uint32_t)indices.size();

        system->arrow_renderable.initialize_rendering(
            render->my, render->rc, vertex_buffer, index_buffer, vertex_input,
            system->gizmo_mesh_shader, vertex_count);
    }

    {
        static const lm::vec3 vertex_data[] = {
            lm::vec3(-0.5, -0.5, 0),
            lm::vec3(0.5, -0.5, 0),
            lm::vec3(0.5, 0.5, 0),
            lm::vec3(-0.5, 0.5, 0),
        };

        static const uint32_t indices[] = {0, 1, 2, 0, 2, 3};

        my::BufferResource vb_res(my::BufferResource::BufferType::Vertex);
        vb_res.size = sizeof(vertex_data);
        vb_res.usage = my::UsageHint::Static;
        vb_res.data = (void*)&vertex_data[0];

        my::ResourceHandle vertex_buffer = render->rc->alloc(&vb_res, monitoring::systems::Gizmos);

        my::BufferResource ib_res(my::BufferResource::BufferType::Index);
        ib_res.size = sizeof(indices);
        ib_res.usage = my::UsageHint::Static;
        ib_res.data = (void*)&indices[0];

        my::ResourceHandle index_buffer = render->rc->alloc(&ib_res, monitoring::systems::Gizmos);

        my::VertexInputStream streams[] = {
            {InputStreamVertex, vertex_buffer, my::VertexFormat::Float32_3, 0, 12,
             my::VertexRate::PerVertex}};

        my::VertexInputResource vi_res;
        vi_res.indices = index_buffer;
        vi_res.attrib_count = HRZ_ARRAY_COUNT(streams);
        vi_res.attribs = streams;
        my::ResourceHandle vertex_input = render->rc->alloc(&vi_res, monitoring::systems::Gizmos);

        system->circle_renderable.initialize_rendering(
            render->my, render->rc, vertex_buffer, index_buffer, vertex_input,
            system->gizmo_circle_shader, HRZ_ARRAY_COUNT(indices));
    }

    system->line_renderable.initialize_rendering(render);
    system->grid_renderable.init_rendering(render);
}

hrz_proto::GizmoLayer default_layer_data()
{
    uint32_t scene_views_bitset = (1 << SCENE_VIEW_COUNT) - 1;

    hrz_proto::GizmoLayer layer_data;

    // Gizmo default data
    layer_data.set_id(0);
    layer_data.set_size(0.2);
    layer_data.set_size_unit(hrz_proto::UiSizeUnit::UI_SIZE_RELATIVE_TO_SCREEN);
    layer_data.set_bbox_padding(0);
    layer_data.mutable_rotation()->set_w(1.f);
    layer_data.set_visible(true);
    layer_data.mutable_scene_views()->set_bits(scene_views_bitset);

    // Line default data
    hrz_proto::Line* line = layer_data.mutable_line();
    line->set_extent_unit(hrz_proto::UiSizeUnit::UI_SIZE_RELATIVE_TO_SCREEN);
    line->set_extent(2);
    line->set_width(1);
    line->mutable_color()->set_r(1);
    line->mutable_color()->set_g(1);
    line->mutable_color()->set_b(1);
    line->mutable_color()->set_a(1);
    line->mutable_scene_views()->set_bits(scene_views_bitset);

    // Grid default data
    hrz_proto::Grid* grid = layer_data.mutable_grid();
    grid->set_extent_unit(hrz_proto::UiSizeUnit::UI_SIZE_RELATIVE_TO_SCREEN);
    grid->set_extent(1);
    grid->set_cell_size(1);
    grid->mutable_color()->set_r(1);
    grid->mutable_color()->set_g(1);
    grid->mutable_color()->set_b(1);
    grid->mutable_color()->set_a(0);
    grid->mutable_scene_views()->set_bits(scene_views_bitset);

    return layer_data;
}

void register_layer(GizmoLayerSystem* system, SceneModel* scene_model, uint64_t global_layer_id)
{
    assert(system);
    assert(scene_model);

    if (system->layer_ids_to_handles.find(global_layer_id)
        != std::end(system->layer_ids_to_handles))
    {
        // Layer is already registered.
        return;
    }

    auto layer_handle = system->layer_pool.alloc();
    Layer* layer = system->layer_pool.get_object(layer_handle);
    layer->global_layer_id = global_layer_id;

    system->layer_ids_to_handles.insert(std::make_pair(global_layer_id, layer_handle));

    hrz_proto::PathRoot root;
    root.mutable_gizmo_layer()->set_opaque(global_layer_id);
    scene_model::register_element(scene_model, root);

    hrz_proto::GizmoLayer layer_data = default_layer_data();
    layer->gizmo.update_from_model(layer_data);

    SceneModelAccessor accessor(scene_model);
    hrz_proto::GizmoLayerPathBuilder<SceneModelAccessor> builder(accessor, root.gizmo_layer());
    builder.set(layer_data);
}

void unregister_layer(GizmoLayerSystem* system, uint64_t layer_id)
{
    assert(system);
    system->unregistered_layers.insert(layer_id);
}

void notify_model_update(
    GizmoLayerSystem* system,
    uint64_t layer_id,
    scene_model::UpdateType update_type,
    const scene_model::GizmoLayerPath& path)
{
    assert(system);

    if (system->layer_ids_to_handles.find(layer_id) != std::end(system->layer_ids_to_handles))
    {
        system->updated_layers.insert(layer_id);
    }
}

RenderRequest work(
    GizmoLayerSystem* system,
    SceneModel* scene_model,
    ClientMessageQueue* mq,
    hrz_proto::SceneViewIndex main_view_index,
    std::span<const RenderViewInfo> views_infos)
{
    assert(system);

    RenderRequest render_request;

    render_request |= _unregister_layers(system, scene_model);

    for (auto layer_id : system->updated_layers)
    {
        auto it = system->layer_ids_to_handles.find(layer_id);
        if (it != std::end(system->layer_ids_to_handles))
        {
            Layer* layer = system->layer_pool.get_object(it->second);
            if (!layer) continue;

            SceneModelAccessor accessor(scene_model);
            hrz_proto::LayerHandle handle;
            handle.set_opaque(layer_id);
            hrz_proto::GizmoLayerPathBuilder<SceneModelAccessor> builder(accessor, handle);

            layer->gizmo.update_from_model(builder.clone().get());
            render_request.request_visual_render();
        }
    }
    system->updated_layers.clear();

    for (auto it : system->layer_ids_to_handles)
    {
        Layer* layer = system->layer_pool.get_object(it.second);
        if (!layer) continue;

        render_request |=
            layer->gizmo.work(mq, scene_model, it.first, main_view_index, views_infos);
    }

    return render_request;
}

void work_gpu(GizmoLayerSystem*, Render*) {}

void draw(GizmoLayerSystem* system, Render* render, std::span<const RenderViewInfo> views_info)
{
    assert(system && render);

    system->circle_renderable.reset();
    system->torus_renderable.reset();
    system->square_renderable.reset();
    system->arrow_renderable.reset();

    for (const auto& it : system->layer_ids_to_handles)
    {
        Layer* layer = system->layer_pool.get_object(it.second);
        if (!layer) continue;

        layer->gizmo.draw(
            render, views_info, system->circle_renderable, system->torus_renderable,
            system->square_renderable, system->arrow_renderable, system->line_renderable,
            system->grid_renderable);
    }

    system->grid_renderable.draw(render);
    system->line_renderable.draw(render);
    system->torus_renderable.draw(render);
    system->square_renderable.draw(render);
    system->arrow_renderable.draw(render);
    system->circle_renderable.draw(render);
}

bool handle_event(
    GizmoLayerSystem* system,
    const hrz::ViewportEvent& event,
    hrz_proto::SceneViewIndex view_index,
    const hrz::CameraViewInfo& view_info)
{
    assert(system);
    for (const auto& it : system->layer_ids_to_handles)
    {
        Layer* layer = system->layer_pool.get_object(it.second);
        if (!layer) continue;

        if (layer->gizmo.handle_event(event, view_index, view_info))
        {
            return true;
        }
    }
    return false;
}

void collect_shaders(hrz::GpuResourceContext* rc)
{
    {
        my::IndexName attribs[] = {
            {InputStreamVertex, "i_vertex"},
        };

        static const my::IndexName ubos[] = {
            {hrz::UboFrame, "Frame"},
            {UboGizmo, "Gizmo"},
        };

        static const char* outputs[] = {
            "o_color",
        };

        static const my::IndexName samplers[] = {
            {0, "u_scene_depth"},
            {1, "u_peel_depth"},
        };

        my::ShaderResource res{};
        res.name = hrz_shaders::Gizmo_mesh_name;
        res.vertex_source_len = hrz_shaders::Gizmo_mesh_vert_len;
        res.vertex_source = hrz_shaders::Gizmo_mesh_vert;
        res.fragment_source_len = hrz_shaders::Gizmo_mesh_frag_len;
        res.fragment_source = hrz_shaders::Gizmo_mesh_frag;
        res.uniform_block_count = HRZ_ARRAY_COUNT(ubos);
        res.uniform_blocks = ubos;
        res.output_count = HRZ_ARRAY_COUNT(outputs);
        res.outputs = outputs;
        res.attrib_count = HRZ_ARRAY_COUNT(attribs);
        res.attribs = attribs;
        res.sampler_count = HRZ_ARRAY_COUNT(samplers);
        res.samplers = samplers;

        res.initial_state.depth.test = true;
        render::initialize_ui_blending_params(&res.initial_state.color_blend);
        res.initial_state.rasterization.cull_mode = my::RasterizationState::Back;

        rc->alloc(&res, hrz::monitoring::systems::Gizmos);
    }

    {
        my::IndexName attribs[] = {
            {InputStreamVertex, "i_vertex"},
        };

        static const my::IndexName ubos[] = {
            {hrz::UboFrame, "Frame"},
            {UboGizmo, "Gizmo"},
        };

        static const char* outputs[] = {
            "o_color",
        };

        static const my::IndexName samplers[] = {
            {0, "u_scene_depth"},
            {1, "u_peel_depth"},
        };

        my::ShaderResource res{};
        res.name = hrz_shaders::Gizmo_circle_name;
        res.vertex_source_len = hrz_shaders::Gizmo_circle_vert_len;
        res.vertex_source = hrz_shaders::Gizmo_circle_vert;
        res.fragment_source_len = hrz_shaders::Gizmo_circle_frag_len;
        res.fragment_source = hrz_shaders::Gizmo_circle_frag;
        res.uniform_block_count = HRZ_ARRAY_COUNT(ubos);
        res.uniform_blocks = ubos;
        res.output_count = HRZ_ARRAY_COUNT(outputs);
        res.outputs = outputs;
        res.attrib_count = HRZ_ARRAY_COUNT(attribs);
        res.attribs = attribs;
        res.sampler_count = HRZ_ARRAY_COUNT(samplers);
        res.samplers = samplers;

        res.initial_state.depth.test = true;
        render::initialize_ui_blending_params(&res.initial_state.color_blend);
        res.initial_state.rasterization.cull_mode = my::RasterizationState::None;

        rc->alloc(&res, hrz::monitoring::systems::Gizmos);
    }

    LineRenderable::collect_shaders(rc);
}
} // namespace gizmo_layers
} // namespace hrz
