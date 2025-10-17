#include "hrz_core_shadows.h"

#include "camera/hrz_core_camera_system.h"
#include "hrz_core_render.h"
#include "hrz_core_shadow_map.h"

#include <hrz_common_maths.h>
#include <hrz_common_profiling.h>
#include <hrz_fnd_log.h>
#include <hrz_fnd_mem.h>
#include <hrz_protocol_path_builder.h>

namespace
{
enum
{
    MaxCascadeCount = HRZ_S_MAX_SUN_CASCADES,
    ShadowMapSize = HRZ_S_SUN_SHADOW_MAP_SIZE,
};

static constexpr const char* SHADOW_MAP_NAMES[MaxCascadeCount] = {
    "sm_sun_near",
    "sm_sun_medium",
    "sm_sun_far",
    "sm_sun_really_far",
};
} // namespace

namespace hrz
{
struct ShadowsSystem
{
    uint32_t cascade_count;

    lm::dmat4 sun_matrix_from_main_view[MaxCascadeCount];
    lm::dmat4 sun_matrix_cc[MaxCascadeCount];
    lm::dmat4 sun_matrix_view[MaxCascadeCount];
    lm::dmat4 sun_matrix_proj[MaxCascadeCount];
    my::Renderer::ViewId views[MaxCascadeCount];
    std::unique_ptr<hrz::shadow_map::ShadowMapPass> pass;
    bool shadows_enabled = true;
    bool model_updated = false;
    hrz_proto::SceneViewIndex model_scene_view;

    lm::dvec3 ecef_sun_direction;
    lm::dmat4 main_view;
    PerspectiveFrustum perspective;

    double near_plane = 0.0;
    double far_plane = 0.0;
    float plane_dists[MaxCascadeCount + 1];
};

namespace shadows
{
void get_shadow_map_names(const ShadowsSystem* sys, const char* names[MaxCascadeCount])
{
    for (size_t i = 0; i < MaxCascadeCount; ++i)
    {
        names[i] = sys->pass->depth_output(i);
    }
}

ShadowsSystem* create(uint32_t cascade_count)
{
    ShadowsSystem* sys = new ShadowsSystem();
    sys->cascade_count = hrz::clamp(cascade_count, (uint32_t)1, (uint32_t)MaxCascadeCount);
    return sys;
}

void init_render(ShadowsSystem* sys, RenderView* render, const char* camera_height_name)
{
    assert(sys && render);

    sys->pass.reset(new hrz::shadow_map::ShadowMapPass(
        "sun shadows maps", camera_height_name, SHADOW_MAP_NAMES, MaxCascadeCount,
        sys->cascade_count, ShadowMapSize, hrz::RenderShadows));
    render->rg->add_pass("sun shadows maps", sys->pass.get());
}

void destroy(ShadowsSystem* sys, Render* render)
{
    assert(sys && render);
    sys->pass->destroy(render->my);
    delete sys;
}

void update(
    ShadowsSystem* sys,
    const CameraViewInfo& cam_view,
    const lm::dvec3 ecef_sun_direction,
    lm::vec2 near_far,
    SceneModel* model)
{
    assert(sys);

    HRZ_SCOPED_SAMPLE("update shadows");

    bool must_recompute_frusta = false;

    {
        float near_shadows = near_far.x * 0.9f;
        float far_shadows = near_far.y * 1.1f;

        // Degenerate case, use default values
        if (near_shadows >= far_shadows)
        {
            near_shadows = 1.0f;
            far_shadows = 5000.0f;
        }

        near_shadows = std::max(1.0f, near_shadows);
        far_shadows = std::min(5000.0f, far_shadows);

        if (near_shadows != sys->near_plane || far_shadows != sys->far_plane)
        {
            float cascade_step = pow(far_shadows / near_shadows, 1.0 / (float)sys->cascade_count);

            sys->plane_dists[0] = near_shadows;
            for (uint32_t i = 1; i <= sys->cascade_count; ++i)
            {
                sys->plane_dists[i] = sys->plane_dists[i - 1] * cascade_step;
            }

            sys->far_plane = far_shadows;
            sys->near_plane = near_shadows;
            must_recompute_frusta = true;
        }
    }

    if (sys->model_updated)
    {
        SceneModelAccessor accessor(model);
        hrz_proto::SceneViewSettingsPathBuilder<SceneModelAccessor> builder(
            accessor, sys->model_scene_view);
        hrz_proto::AmbientSettings settings = builder.ambient().get();

        sys->shadows_enabled = settings.lighting().cast_shadows()
            && settings.lighting().receive_shadows() && settings.lighting().enable_lighting();

        sys->model_updated = false;
    }

    if (ecef_sun_direction != sys->ecef_sun_direction)
    {
        sys->ecef_sun_direction = ecef_sun_direction;
        must_recompute_frusta = true;
    }

    if (cam_view.cam.view != sys->main_view)
    {
        sys->main_view = cam_view.cam.view;
        must_recompute_frusta = true;
    }

    PerspectiveFrustum current_frustum = cam_view.to_perspective_frustum();
    if (current_frustum != sys->perspective)
    {
        sys->perspective = current_frustum;
        must_recompute_frusta = true;
    }

    if (must_recompute_frusta)
    {
        // We need to compute a base for projecting the frustum points in the shadow
        // maps plane. We know that the sun direction will never be aligned with the
        // Z axis so we can use this safely, for once!
        lm::dvec3 sm_z_axis = lm::normalize(sys->ecef_sun_direction);
        lm::dvec3 sm_x_axis = lm::normalize(lm::cross(lm::dvec3(0, 0, 1), sm_z_axis));
        lm::dvec3 sm_y_axis = lm::normalize(lm::cross(sm_z_axis, sm_x_axis));

        // Here we compute a negative offset for the near plane of each shadow frusta.
        // This is used to include geometry outside the main camera frustum.
        // It depends on the angle of the sun because but it's mostly empirically computed.
        double sun_normal_dot =
            std::abs(lm::dot(sys->ecef_sun_direction, lm::normalize(cam_view.cam.pos)));
        double sm_near_bias = -(1000.0 + (1.0 - sun_normal_dot) * 2000.0);

        for (size_t cascade = 0; cascade < sys->cascade_count; ++cascade)
        {
            lm::dmat4 projection = lm::perspective_subfrustum_opengl<double>(
                sys->perspective.fovy, sys->perspective.aspect_ratio,
                sys->perspective.subfrustum.min.x * 2 - 1,
                sys->perspective.subfrustum.max.x * 2 - 1,
                sys->perspective.subfrustum.min.y * 2 - 1,
                sys->perspective.subfrustum.max.y * 2 - 1, (double)sys->plane_dists[cascade],
                (double)sys->plane_dists[cascade + 1]);

            lm::dmat4 proj_view_inv = lm::inverse(projection * sys->main_view);

            static const lm::dvec4 CORNERS_NDC[8] = {
                {1, 1, 1, 1},  {-1, 1, 1, 1},  {1, -1, 1, 1},  {-1, -1, 1, 1},
                {1, 1, -1, 1}, {-1, 1, -1, 1}, {1, -1, -1, 1}, {-1, -1, -1, 1},
            };

            lm::dvec2 corners[8];
            double min_z = std::numeric_limits<double>::max();
            double max_z = std::numeric_limits<double>::lowest();

            for (int j = 0; j < 8; ++j)
            {
                // Find the coordinates of the corners of the frustum in world space.
                lm::dvec4 pt_homogeneous = proj_view_inv * CORNERS_NDC[j];
                lm::dvec3 pt = pt_homogeneous.xyz / pt_homogeneous.w;

                // Then reproject them along the shadow map axes.
                corners[j] = lm::dvec2(lm::dot(sm_x_axis, pt), lm::dot(sm_y_axis, pt));

                double z = lm::dot(sm_z_axis, pt);
                if (z < min_z) min_z = z;
                if (z > max_z) max_z = z;
            }

            // This is the minimum oriented bounding box of the corners of the
            // view frustum, in the shadow map space.
            OrientedBBox2<double> obb = compute_minimum_bbox(std::span<const lm::dvec2>(corners));

            lm::dvec3 sm_eye =
                sm_z_axis * max_z + sm_x_axis * obb.center.x + sm_y_axis * obb.center.y;

            lm::dquat rotation = lm::axis_angle(sm_z_axis, obb.angle);
            lm::dvec3 sm_up = rotation * sm_y_axis;

            lm::dvec3 sm_target = sm_eye - sm_z_axis * 100000.0;

            lm::dmat4 sm_view = lm::view(sm_eye, sm_target, sm_up);

            lm::dmat4 sm_proj = lm::orthographic_opengl(
                obb.half_width * 2, obb.half_height * 2, sm_near_bias, max_z - min_z);

            sys->sun_matrix_view[cascade] = sm_view;
            sys->sun_matrix_proj[cascade] = sm_proj;
        }

        lm::dmat4 inverse_cam_view = lm::inverse(sys->main_view);
        lm::dmat4 cam_pos_translation = lm::translation(cam_view.cam.pos);

        for (size_t i = 0; i < sys->cascade_count; ++i)
        {
            lm::dmat4 pv = sys->sun_matrix_proj[i] * sys->sun_matrix_view[i];
            sys->sun_matrix_from_main_view[i] = pv * inverse_cam_view;
            sys->sun_matrix_cc[i] = pv * cam_pos_translation;
        }
    }

    sys->pass->schedule_render_all();
    sys->pass->set_enabled(sys->shadows_enabled);
}

lm::dmat4 get_sun_matrix(const ShadowsSystem* sys, size_t cascade)
{
    assert(cascade < sys->cascade_count && sys);
    return sys->sun_matrix_from_main_view[cascade];
}

void register_views(
    ShadowsSystem* sys,
    Render* render,
    std::vector<my::Renderer::ViewId>& created_views)
{
    assert(sys && render);

    for (size_t i = 0; i < sys->cascade_count; ++i)
    {
        sys->views[i] = render->rd->add_auxiliary_view(
            my::View{sys->sun_matrix_proj[i], sys->sun_matrix_view[i]}, hrz::RenderAllWorldBins);
        sys->pass->set_view(i, sys->views[i]);
        created_views.push_back(sys->views[i]);
    }
}

void draw(ShadowsSystem* sys, Render* render)
{
    assert(sys && render);

    HRZ_SCOPED_SAMPLE("shadows draw");

    for (size_t i = 0; i < sys->cascade_count; ++i)
    {
        sys->pass->update_ubo(i, render, sys->sun_matrix_from_main_view[i], sys->sun_matrix_cc[i]);
    }
}

bool are_shadows_enabled(const ShadowsSystem* sys)
{
    return sys->shadows_enabled;
}

size_t get_cascade_count(const ShadowsSystem* sys)
{
    return sys->cascade_count;
}

double get_shadow_map_far(const ShadowsSystem* sys)
{
    return sys->far_plane;
}

void notify_model_update(
    ShadowsSystem* sys,
    scene_model::UpdateType update_type,
    const scene_model::SceneViewSettingsPath& path)
{
    if (path.leaf() || path.is_ambient())
    {
        sys->model_updated = true;
        sys->model_scene_view = path.get_root();
    }
}

} // namespace shadows
} // namespace hrz
