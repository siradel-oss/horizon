#include "hrz/core/grid.h"

#include "hrz/common/color.h"
#include "hrz/common/profiling.h"
#include "hrz/core/render/defs.h"
#include "hrz/core/render/double_buffered_uniform_buffer.h"
#include "hrz/core/render/resources.h"
#include "hrz/core/render/screen_space.h"
#include "hrz/core/shaders/collection.h"
#include "hrz/fnd/log.h"
#include "hrz/fnd/mem.h"

#include <vector>

namespace
{
enum
{
    UboGrid = hrz::UboCustomStart,

    VertexInputStream = 0,
};

struct GridUniformData
{
    lm::vec4 color;
    lm::vec3 axis_x;
    float extent;
    lm::vec3 axis_y;
    float cell_size;
    lm::vec3 ecef_cc_pos;
    uint32_t _padding_1[1];
    lm::vec2 offset;
    uint32_t _padding_2[2];
};

HRZ_CHECK_UBO_SIZE(GridUniformData);
} // anonymous namespace

namespace hrz
{
namespace grid
{
class GridRenderable : public my::Renderer::Renderable
{
    static const uint32_t MaxInstanceCount = 32;

    struct InstanceData
    {
        lm::dvec3 center;
        double radius;
        size_t ubo_offset;
        uint32_t scene_views;
    };

    render::DoubleBufferedUniformBuffer<GridUniformData> _ubo;
    std::vector<InstanceData> _instances;

    my::ResourceHandle _vertex_buffer = my::ResourceHandle::null();
    my::ResourceHandle _vertex_input = my::ResourceHandle::null();
    my::ResourceHandle _visual_shader = my::ResourceHandle::null();

    struct RenderData
    {
        my::ResourceHandle vertex_input = my::ResourceHandle::null();
        my::ResourceHandle visual_shader = my::ResourceHandle::null();
        my::ResourceHandle ubo = my::ResourceHandle::null();

        uint32_t scene_views;
        size_t ubo_offset;
    };

    static void render_callback(
        uint32_t render_type,
        my::RenderContext* r,
        my::ResourceBinder* rb,
        const void* user_data_raw,
        const void* raw_data)
    {
        auto data = (const RenderData*)raw_data;
        const auto* user_data = (const hrz::SceneViewRenderGraphUserData*)user_data_raw;

        if (render_type != hrz::RenderVisual) return;
        if (((1 << user_data->scene_view) & data->scene_views) == 0) return;

        auto batch = my::DrawBatchInfo(my::PrimitiveType::TriangleStrip, 4);

        rb->push_state();

        my::UboBinding ubo_bindings[] = {
            {UboGrid, data->ubo, (uint32_t)data->ubo_offset, sizeof(GridUniformData)},
        };
        rb->bind(HRZ_ARRAY_COUNT(ubo_bindings), ubo_bindings);

        auto state = rb->get_current_state();

        r->draw(
            batch, data->visual_shader, data->vertex_input, state.ubo_count, state.ubos,
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
                RenderData render_data;
                render_data.vertex_input = _vertex_input;
                render_data.ubo = _ubo.get_for_gpu();
                render_data.visual_shader = _visual_shader;
                render_data.ubo_offset = instance.ubo_offset;
                render_data.scene_views = instance.scene_views;

                queue.enqueue(
                    hrz::RenderUiBin, render_callback, &render_data, instance.center,
                    instance.radius);
            }
        }
    }

public:
    GridRenderable() : _ubo(MaxInstanceCount) {}

    static void collect_shaders(hrz::GpuResourceContext* rc)
    {
        {
            my::IndexName attribs[] = {
                {VertexInputStream, "i_vertex"},
            };

            static const my::IndexName ubos[] = {
                {hrz::UboFrame, "Frame"},
                {UboGrid, "Grid"},
            };

            static const my::IndexName samplers[] = {
                {0, "u_scene_depth"},
                {1, "u_peel_depth"},
            };

            static const char* outputs[] = {"o_color"};

            my::ShaderResource res{};
            res.name = hrz_shaders::Grid_name;
            res.vertex_source_len = hrz_shaders::Grid_vert_len;
            res.vertex_source = hrz_shaders::Grid_vert;
            res.fragment_source_len = hrz_shaders::Grid_frag_len;
            res.fragment_source = hrz_shaders::Grid_frag;
            res.uniform_block_count = HRZ_ARRAY_COUNT(ubos);
            res.uniform_blocks = ubos;
            res.output_count = HRZ_ARRAY_COUNT(outputs);
            res.outputs = outputs;
            res.attrib_count = HRZ_ARRAY_COUNT(attribs);
            res.attribs = attribs;
            res.sampler_count = HRZ_ARRAY_COUNT(samplers);
            res.samplers = samplers;

            res.initial_state.depth.test = true;
            res.initial_state.depth.compare = my::DepthState::Compare::LessEqual;
            res.initial_state.rasterization.cull_mode = my::RasterizationState::None;

            render::initialize_ui_blending_params(&res.initial_state.color_blend);

            rc->alloc(&res, hrz::monitoring::systems::Grid);
        }
    }

    void initialize_rendering(hrz::Render* render)
    {
        // @Note @Performance: The API exposes a Grid object to allow rendering grids. A single Grid
        // object can be used to render multiple grids. Though, each time a new object is created we
        // need to initialize all the GPU resources which can already exist in some other Grid
        // instance. Right now only the Gizmos and the ClippingPlaneLayer have a Grid instance
        // but if this starts to get used in lots of other places it could be interesting to
        // make a more general grid system to allocate resources only once.
        {
            static const lm::vec2 vertices[] = {{1, -1}, {1, 1}, {-1, -1}, {-1, 1}};

            my::BufferResource vb_res(my::BufferResource::BufferType::Vertex);
            vb_res.size = sizeof(vertices);
            vb_res.usage = my::UsageHint::Static;
            vb_res.data = &vertices[0].x;

            _vertex_buffer = render->rc->alloc(&vb_res, monitoring::systems::Gizmos);
        }

        {
            my::VertexInputStream streams[] = {
                {VertexInputStream, _vertex_buffer, my::VertexFormat::Float32_2, 0, 0,
                 my::VertexRate::PerVertex},
            };

            my::VertexInputResource vi_res;
            vi_res.attrib_count = HRZ_ARRAY_COUNT(streams);
            vi_res.attribs = streams;

            _vertex_input = render->rc->alloc(&vi_res, monitoring::systems::Gizmos);
        }

        _ubo.initialize(
            render, monitoring::systems::DevTools,
            {{"contents"_ss, "grid instance uniform data"_ss}});

        _visual_shader = render->rc->retrieve_shader(hrz_shaders::Grid_name);
    }

    void add_instance(
        const lm::dvec3& center,
        double radius,
        uint32_t scene_views,
        const GridUniformData& uniform_data)
    {
        if (_instances.size() >= MaxInstanceCount)
        {
            HRZ_LOG_WARNING("Maximum number of instances reached.");
            return;
        }

        size_t instance_index = _instances.size();
        _ubo.set(instance_index, uniform_data);

        InstanceData instance_data;
        instance_data.center = center;
        instance_data.radius = radius;
        instance_data.scene_views = scene_views;
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

    void destroy(my::ResourceContext* rc)
    {
        rc->dealloc(_vertex_buffer);
        rc->dealloc(_vertex_input);
        _ubo.destroy(rc);
    }

    void reset() { _instances.clear(); }
};

// We have to define this because GridRenderable is forward-declared in the header file, so the
// destructor cannot be auto otherwise auto files using Grid couldn't destroy it. Weird people would
// say that GridRenderable should be defined with a virtual destructor in the header file, but they
// are weird.
// https://chrizog.com/cpp-pimpl-unique-ptr-incomplete-types-default-constructor
//    -slerouzic, 2024-05-20
Grid::Grid() = default;
Grid::~Grid() = default;

void Grid::init_rendering(Render* render)
{
    HRZ_SCOPED_SAMPLE("grid init rendering");
    assert(render);

    if (_renderable) return;

    _renderable = std::make_unique<GridRenderable>();
    _renderable->initialize_rendering(render);
}

void Grid::deinit_rendering(Render* render)
{
    HRZ_SCOPED_SAMPLE("grid deinit rendering");
    assert(render);

    if (_renderable)
    {
        _renderable->destroy(render->my);
    }
}

bool Grid::is_rendering_initialized() const
{
    return _renderable != nullptr;
}

void Grid::schedule_draw(
    const GridParams& params,
    const lm::dvec3& ecef_pos,
    const lm::dvec3& ecef_cc_pos,
    const lm::vec3& axis_x,
    const lm::vec3& axis_y,
    const lm::vec2& offset,
    const CameraViewInfo& view_info,
    uint32_t scene_views)
{
    HRZ_SCOPED_SAMPLE("grid draw");

    if (!_renderable) return;

    GridUniformData uniform_data;
    uniform_data.axis_x = axis_x;
    uniform_data.axis_y = axis_y;
    uniform_data.ecef_cc_pos = lm::vec3(ecef_cc_pos);
    uniform_data.offset = offset;
    uniform_data.cell_size = params.cell_size;
    uniform_data.color = params.color;

    switch (params.extent_unit)
    {
        case hrz_proto::UiSizeUnit::UI_SIZE_IN_PIXELS:
            uniform_data.extent =
                params.extent * render::compute_logical_pixels_to_meters(ecef_pos, view_info);
            break;

        case hrz_proto::UiSizeUnit::UI_SIZE_RELATIVE_TO_SCREEN:
            uniform_data.extent = params.extent
                * render::compute_device_pixels_to_meters(ecef_pos, view_info)
                * std::min(view_info.viewport.size.x, view_info.viewport.size.y);
            break;

        case hrz_proto::UiSizeUnit::UI_SIZE_IN_METERS:
        default: uniform_data.extent = params.extent; break;
    }

    _renderable->add_instance(ecef_pos, params.extent, scene_views, uniform_data);
}

void Grid::draw(Render* render)
{
    HRZ_SCOPED_SAMPLE("grid draw");
    assert(render);

    if (!_renderable) return;

    _renderable->draw(render);
    _renderable->reset();
}

void collect_shaders(hrz::GpuResourceContext* rc)
{
    GridRenderable::collect_shaders(rc);
}
} // namespace grid

grid::GridParams from_proto(const hrz_proto::Grid& grid)
{
    grid::GridParams grid_params;
    grid_params.extent = grid.extent();
    grid_params.cell_size = grid.cell_size();
    grid_params.color = hrz::srgb_to_linear(hrz::convert_proto_color_to_float(grid.color()));
    grid_params.scene_views_bitset = grid.scene_views().bits();
    grid_params.extent_unit = grid.extent_unit();
    return grid_params;
}
} // namespace hrz
