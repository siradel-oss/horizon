#include "hrz/core/viewsheds.h"

#include "hrz/common/color.h"
#include "hrz/common/monitoring_defs.h"
#include "hrz/common/profiling.h"
#include "hrz/common/proto_geo.h"
#include "hrz/core/render/common_ubos.h"
#include "hrz/core/render/context.h"
#include "hrz/core/render/defs.h"
#include "hrz/core/render/resource_context.h"
#include "hrz/core/shaders/collection.h"
#include "hrz/core/shadow_map.h"
#include "hrz/fnd/mem.h"
#include "hrz/protocol/path_builder/scene/view_settings.h"

namespace
{
enum
{
    ViewshedCount = HRZ_S_VIEWSHED_CNT,
    ShadowMapSize = HRZ_S_VIEWSHED_SHADOW_MAP_SIZE,
};

static constexpr const char* SHADOW_MAP_NAMES[ViewshedCount] = {
    "sm_viewshed",
};

struct Cube : public my::Renderer::Renderable
{
    lm::dvec3 center;
    lm::dmat4 inv_pv;
    lm::dmat4 main_cam_pv;
    bool draw_from_main_cam = false;
    my::ResourceHandle vertex_buffer;
    my::ResourceHandle index_buffer;

    lm::dvec4 vertices[8] = {
        {-1, -1, -1, 1}, {1, -1, -1, 1}, {-1, 1, -1, 1}, {1, 1, -1, 1},
        {-1, -1, 1, 1},  {1, -1, 1, 1},  {-1, 1, 1, 1},  {1, 1, 1, 1},
    };

    struct RenderData
    {
        my::ResourceHandle ubo;
        my::ResourceHandle vertex_input;
        my::ResourceHandle shader;
        hrz_proto::SceneViewIndex scene_view;
    } data;

    static void draw_cube(
        uint32_t render_type,
        my::RenderContext* r,
        my::ResourceBinder* rb,
        const void* user_data_raw,
        const void* render_data_raw)
    {
        auto data = (const RenderData*)render_data_raw;
        my::ResourceHandle shader;

        auto user_data = (const hrz::SceneViewRenderGraphUserData*)user_data_raw;
        if (user_data->scene_view != data->scene_view) return;

        switch (render_type)
        {
            case hrz::RenderVisual: shader = data->shader; break;
            default: return;
        }

        auto batch =
            my::DrawBatchInfo(my::PrimitiveType::LineList, 24).indexed(my::IndexType::UByte);

        rb->push_state();

        auto state = rb->get_current_state();

        r->draw(batch, shader, data->vertex_input, state.ubos, {});

        rb->pop_state();
    }

    void collect_render_info(my::Renderer::Queue& queue, const my::Renderer::Culler& culler)
        const override
    {
        queue.enqueue(hrz::RenderWorldOpaqueBinBit, draw_cube, data, center, 10000);
    }

    static void collect_shaders(hrz::GpuResourceContext* rc)
    {
        my::IndexName attribs[] = {{0, "i_vertex"}};

        static const my::IndexName uniform_blocks[] = {{hrz::UboFrame, "Frame"}};

        static const char* outputs[] = {
            "o_color",
        };

        my::ShaderResource res{};
        res.name = hrz_shaders::ViewshedWireframe_name;
        res.attribs = attribs;
        res.vertex_source_len = hrz_shaders::ViewshedWireframe_vert_len;
        res.vertex_source = hrz_shaders::ViewshedWireframe_vert;
        res.fragment_source_len = hrz_shaders::ViewshedWireframe_frag_len;
        res.fragment_source = hrz_shaders::ViewshedWireframe_frag;
        res.uniform_blocks = uniform_blocks;
        res.samplers = {};
        res.outputs = outputs;
        res.initial_state.depth.test = true;
        res.initial_state.color_blend.enable = true;
        res.initial_state.color_blend.color.src = my::ColorBlendState::One;
        res.initial_state.color_blend.color.dst = my::ColorBlendState::OneMinusSrcAlpha;
        res.initial_state.color_blend.alpha.src = my::ColorBlendState::One;
        res.initial_state.color_blend.alpha.dst = my::ColorBlendState::OneMinusSrcAlpha;

        rc->alloc(&res, hrz::monitoring::systems::Viewsheds);
    }

    void init(hrz::RenderView* render)
    {
        {
            my::BufferResource res(my::BufferResource::Uniform);
            res.size = sizeof(hrz::FrameUniformData);
            res.usage = my::UsageHint::Updatable;
            res.data = nullptr;
            data.ubo = render->rc->alloc(&res, hrz::monitoring::systems::Viewsheds);
        }

        data.shader = render->rc->retrieve_shader(hrz_shaders::ViewshedWireframe_name);

        {
            uint8_t indices[] = {0, 1, 0, 2, 1, 3, 2, 3, 4, 5, 4, 6,
                                 5, 7, 6, 7, 0, 4, 1, 5, 2, 6, 3, 7};

            my::BufferResource vb_res(my::BufferResource::BufferType::Vertex);
            vb_res.size = sizeof(vertices);
            vb_res.usage = my::UsageHint::Static;
            vb_res.data = nullptr;

            my::BufferResource ib_res(my::BufferResource::Index);
            ib_res.size = sizeof(indices);
            ib_res.data = indices;
            ib_res.usage = my::UsageHint::Static;

            vertex_buffer = render->rc->alloc(&vb_res, hrz::monitoring::systems::Viewsheds);
            index_buffer = render->rc->alloc(&ib_res, hrz::monitoring::systems::Viewsheds);

            my::VertexInputStream streams[] = {
                {0, vertex_buffer, my::VertexFormat::Float32_4, 0, 0, my::VertexRate::PerVertex}};

            my::VertexInputResource vi_res;
            vi_res.indices = index_buffer;
            vi_res.attribs = streams;

            data.vertex_input = render->rc->alloc(&vi_res, hrz::monitoring::systems::Viewsheds);
        }
    }

    void draw(hrz::RenderView* render, hrz_proto::SceneViewIndex scene_view)
    {
        {
            lm::vec4 main_clip_vertices[8];

            if (draw_from_main_cam)
            {
                for (size_t i = 0; i < 4; ++i)
                {
                    main_clip_vertices[i] = lm::vec4(main_cam_pv * lm::dvec4(center, 1.0));
                }
            }

            for (size_t i = draw_from_main_cam ? 4 : 0; i < 8; ++i)
            {
                lm::dvec4 dclip_vertex = inv_pv * vertices[i];
                dclip_vertex /= dclip_vertex.w;
                dclip_vertex = main_cam_pv * dclip_vertex;
                main_clip_vertices[i] = lm::vec4(dclip_vertex);
            }

            render->my->update_buffer(
                vertex_buffer, 0, sizeof(main_clip_vertices), main_clip_vertices);
        }

        data.scene_view = scene_view;
        render->rd->collect_renderable(*this);
    }

    void destroy(my::ResourceContext* rc)
    {
        rc->dealloc(vertex_buffer);
        rc->dealloc(data.ubo);
        rc->dealloc(index_buffer);
        rc->dealloc(data.vertex_input);
    }
};
} // namespace

namespace hrz
{
struct ViewshedsSystem
{
    lm::dvec3 viewshed_position[ViewshedCount];
    lm::dvec3 viewshed_position_from_main_view[ViewshedCount];
    lm::dmat4 viewshed_matrix_from_main_view[ViewshedCount];
    lm::dmat4 viewshed_matrix_cc[ViewshedCount];
    lm::dmat4 viewshed_matrix_view[ViewshedCount];
    lm::dmat4 viewshed_matrix_proj[ViewshedCount];
    my::Renderer::ViewId views[ViewshedCount];
    std::unique_ptr<hrz::shadow_map::ShadowMapPass> pass;

    lm::vec4 visible_color[ViewshedCount];
    lm::vec4 hidden_color[ViewshedCount];

    bool viewshed_enabled;
    bool viewshed_wireframe_enabled;
    bool model_updated;
    hrz_proto::SceneViewIndex model_scene_view;
    Cube cube;
};

namespace viewsheds
{
bool is_viewshed_enabled(const ViewshedsSystem* sys)
{
    return sys->viewshed_enabled;
}

void get_shadow_map_names(const ViewshedsSystem* sys, const char* names[HRZ_S_VIEWSHED_CNT])
{
    for (size_t i = 0; i < ViewshedCount; ++i)
    {
        names[i] = sys->pass->depth_output(i);
    }
}

ViewshedsSystem* create()
{
    ViewshedsSystem* sys = new ViewshedsSystem();
    return sys;
}

void init_render(ViewshedsSystem* sys, RenderView* render, const char* input_camera_height)
{
    assert(sys && render);

    sys->pass.reset(new hrz::shadow_map::ShadowMapPass(
        "viewshed depth maps", input_camera_height, SHADOW_MAP_NAMES, ViewshedCount, ViewshedCount,
        HRZ_S_VIEWSHED_SHADOW_MAP_SIZE, hrz::RenderViewshed));
    render->rg->add_pass("viewshed depth maps", sys->pass.get());

    sys->cube.init(render);
}

void destroy(ViewshedsSystem* sys, RenderView* render)
{
    assert(sys && render);
    sys->pass->destroy(render->my);
    sys->cube.destroy(render->my);
    delete sys;
}

RenderRequest update(ViewshedsSystem* sys, const CameraViewInfo& cam, SceneModel* model)
{
    assert(sys);
    HRZ_SCOPED_SAMPLE("update viewshed");

    RenderRequest render_request;

    if (sys->model_updated)
    {
        auto settings = hrz_proto::SceneViewSettingsPathBuilder<SceneModelAccessor>(
                            model, sys->model_scene_view)
                            .viewshed()
                            .get();

        //@Todo Extends it for multiple Viewsheds
        sys->viewshed_enabled = settings.enable_viewshed();
        sys->viewshed_wireframe_enabled = settings.enable_wireframe();

        lm::dmat4 vs_view = enu_to_ecef_transform_for_geo(from_proto(settings.position()));
        sys->cube.center = vs_view.w.xyz;
        sys->viewshed_position[0] = vs_view.w.xyz;
        // for bearing on y-axis and tilt on x-axis.
        vs_view = vs_view * lm::rotation({1, 0, 0}, lm::PI * 0.5);
        vs_view = vs_view * lm::rotation({0, 1, 0}, -(double)settings.bearing())
            * lm::rotation({1, 0, 0}, (double)settings.tilt());

        double fov_v =
            horizontal_to_vertical_fov(lm::radians(settings.hfov()), settings.aspect_ratio());

        double n = (double)settings.start() * settings.max_distance();
        double f = settings.max_distance();
        lm::dmat4 vs_proj = lm::perspective_opengl(fov_v, (double)settings.aspect_ratio(), n, f);

        sys->viewshed_matrix_view[0] = lm::inverse(vs_view);
        sys->viewshed_matrix_proj[0] = vs_proj;

        sys->cube.draw_from_main_cam = settings.draw_wireframe_from_position();

        auto visible_color = hrz::premultiply_alpha(
            hrz::srgb_to_linear(hrz::convert_proto_color_to_float(settings.visible_color())));
        sys->visible_color[0] = visible_color;
        auto hidden_color = hrz::premultiply_alpha(
            hrz::srgb_to_linear(hrz::convert_proto_color_to_float(settings.hidden_color())));
        sys->hidden_color[0] = hidden_color;

        sys->model_updated = false;
        render_request.request_visual_render();
    }

    if (!sys->viewshed_enabled) return render_request;

    lm::dmat4 inverse_cam_view = lm::inverse(cam.cam.view);
    lm::dmat4 cam_pos_translation = lm::translation(cam.cam.pos);

    for (size_t i = 0; i < ViewshedCount; ++i)
    {
        lm::dmat4 pv = sys->viewshed_matrix_proj[i] * sys->viewshed_matrix_view[i];
        sys->viewshed_position_from_main_view[i] =
            (cam.cam.view * lm::dvec4(sys->viewshed_position[i], 1)).xyz;
        sys->viewshed_matrix_from_main_view[i] = pv * inverse_cam_view;
        sys->viewshed_matrix_cc[i] = pv * cam_pos_translation;
        sys->cube.inv_pv = lm::inverse(pv);
        sys->cube.main_cam_pv = cam.pv;
    }

    sys->pass->schedule_render_all();
    sys->pass->set_enabled(sys->viewshed_enabled);

    return render_request;
}

void notify_model_update(
    ViewshedsSystem* sys,
    scene_model::UpdateType,
    const scene_model::SceneViewSettingsPath& path)
{
    if (path.leaf() || path.is_viewshed())
    {
        sys->model_updated = true;
        sys->model_scene_view = path.get_root();
    }
}

lm::dmat4 get_viewshed_matrix(const ViewshedsSystem* sys, size_t index)
{
    assert(index < ViewshedCount && sys);
    return sys->viewshed_matrix_from_main_view[index];
}

lm::dvec3 get_viewshed_position_from_main_view(const ViewshedsSystem* sys, size_t index)
{
    assert(index < ViewshedCount && sys);
    return sys->viewshed_position_from_main_view[index];
}

void get_viewshed_colors(const ViewshedsSystem* sys, size_t index, lm::vec4 colors[2])
{
    assert(index < ViewshedCount && sys);
    colors[0] = sys->visible_color[index];
    colors[1] = sys->hidden_color[index];
}

void register_views(
    ViewshedsSystem* sys,
    RenderView* render,
    std::vector<my::Renderer::ViewId>& created_views)
{
    assert(sys && render);
    for (size_t i = 0; i < ViewshedCount; ++i)
    {
        sys->views[i] = render->rd->add_auxiliary_view(
            my::View{sys->viewshed_matrix_proj[i], sys->viewshed_matrix_view[i]},
            hrz::RenderAllWorldBins);
        sys->pass->set_view(i, sys->views[i]);
        created_views.push_back(sys->views[i]);
    }
}

void draw(ViewshedsSystem* sys, RenderView* render)
{
    HRZ_SCOPED_SAMPLE("viewshed draw");

    assert(sys && render);
    if (sys->viewshed_enabled)
    {
        for (size_t i = 0; i < ViewshedCount; ++i)
        {
            sys->pass->update_ubo(
                i, render, sys->viewshed_matrix_from_main_view[i], sys->viewshed_matrix_cc[i]);
        }
    }

    if (sys->viewshed_enabled && sys->viewshed_wireframe_enabled)
    {
        sys->cube.draw(render, sys->model_scene_view);
    }
}

void collect_shaders(hrz::GpuResourceContext* rc)
{
    Cube::collect_shaders(rc);
}
} // namespace viewsheds
} // namespace hrz
