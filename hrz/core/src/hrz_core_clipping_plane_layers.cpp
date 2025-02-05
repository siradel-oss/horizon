#include "hrz_core_clipping_plane_layers.h"

#include "hrz_core_grid.h"
#include "hrz_core_render.h"
#include "hrz_core_shaders.h"

#include <hrz_common_proto_geo.h>
#include <hrz_common_proto_maths.h>
#include <hrz_fnd_flat_hash_map.h>
#include <hrz_fnd_flat_hash_set.h>
#include <hrz_fnd_gen_object_pool.h>
#include <hrz_fnd_mem.h>
#include <hrz_protocol_path_builder.h>

#include <optional>

namespace hrz
{
struct ClippingPlane
{
    int32_t clip_id;
    lm::dmat4 view;
    lm::dmat4 inv_view;
    lm::dvec3 normal;

    lm::vec4 outline_color;
    float outline_distance;

    bool show_plane;
    grid::GridParams grid_params;

    bool recompute_axis;
    lm::vec3 cached_axis_x;
    lm::vec3 cached_axis_y;
};

struct ClippingPlaneLayerSystem
{
    using IndexPool = GenIndexPool<uint32_t, 16, 16>;
    using ClipPool = GenObjectPool<ClippingPlane, IndexPool, 64>;

    ClippingPlaneInfo default_plane;

    ClipPool layer_pool;
    hrz::flat_hash_map<uint64_t, uint32_t> layer_ids_to_handles;

    hrz::flat_hash_set<uint64_t> updated_layers;
    hrz::flat_hash_set<uint64_t> unregistered_layers;

    hrz::flat_hash_set<uint64_t> all_active_planes;
    hrz::flat_hash_set<uint64_t> to_render;

    grid::Grid grid_renderable;
};

namespace clipping_plane_layers
{
namespace
{
inline bool _is_clip_id_valid(int32_t clip_id)
{
    return clip_id >= 0 && clip_id < HRZ_S_MAX_CLIP_PLANES;
}

ClippingPlane* _get_layer(ClippingPlaneLayerSystem* system, uint64_t layer_id)
{
    auto it = system->layer_ids_to_handles.find(layer_id);
    if (it == system->layer_ids_to_handles.end()) return nullptr;

    return system->layer_pool.get_object(it->second);
}

inline bool _is_clip_plane_usable(const ClippingPlane* layer)
{
    return layer && _is_clip_id_valid(layer->clip_id) && (layer->normal != lm::dvec3(0));
}

RenderRequest _update_layer(ClippingPlaneLayerSystem* system, SceneModel* model, uint64_t layer_id)
{
    RenderRequest render_request;

    auto layer = _get_layer(system, layer_id);
    if (!layer) return render_request;

    SceneModelAccessor accessor(model);

    hrz_proto::LayerHandle handle;
    handle.set_opaque(layer_id);

    hrz_proto::ClippingPlaneLayerPathBuilder<SceneModelAccessor> builder(accessor, handle);

    auto data = builder.clone().get();
    int32_t new_clip_id = data.clip_id();

    if (_is_clip_id_valid(layer->clip_id))
    {
        system->all_active_planes.erase(layer_id);
    }

    if (_is_clip_id_valid(new_clip_id))
    {
        system->all_active_planes.insert(layer_id);
    }

    layer->clip_id = new_clip_id;
    layer->inv_view = hrz::enu_to_ecef_transform_for_geo(from_proto(data.origin_position()));
    layer->view = lm::inverse(layer->inv_view);
    layer->normal = lm::dvec3(hrz::to_lm(data.normal()));
    layer->outline_color = hrz::to_lm(data.outline_color());
    layer->outline_distance = data.outline_distance();
    layer->show_plane = data.show_plane();
    layer->grid_params = hrz::from_proto(data.grid());

    if (layer->show_plane)
    {
        layer->recompute_axis = true;
        system->to_render.insert(layer_id);
    }
    else
    {
        system->to_render.erase(layer_id);
    }

    render_request.request_visual_render();

    return render_request;
}

RenderRequest _unregister_layers(ClippingPlaneLayerSystem* system, SceneModel* model)
{
    assert(system && model);

    RenderRequest render_request;

    for (auto layer_id : system->unregistered_layers)
    {
        {
            auto it = system->layer_ids_to_handles.find(layer_id);
            if (it != system->layer_ids_to_handles.end())
            {
                hrz_proto::PathRoot root;
                root.mutable_clipping_plane_layer()->set_opaque(layer_id);
                scene_model::unregister_element(model, root);

                auto inner_id = it->second;
                auto layer = system->layer_pool.get_object(inner_id);

                if (layer)
                {
                    if (_is_clip_id_valid(layer->clip_id))
                    {
                        system->all_active_planes.erase(layer_id);
                    }

                    system->layer_pool.release(inner_id);
                }

                system->to_render.erase(inner_id);
                system->layer_ids_to_handles.erase(it);

                render_request.request_visual_render();
            }
        }
    }
    system->unregistered_layers.clear();

    return render_request;
}

hrz_proto::ClippingPlaneLayer _default_layer_data(int32_t clip_id)
{
    uint32_t scene_views_bitset = (1 << SCENE_VIEW_COUNT) - 1;

    hrz_proto::ClippingPlaneLayer layer_data;

    // Clipping plane
    layer_data.set_clip_id(clip_id);
    layer_data.mutable_normal()->set_x(0.0f);
    layer_data.mutable_normal()->set_y(0.0f);
    layer_data.mutable_normal()->set_z(1.0f);
    layer_data.mutable_outline_color()->set_r(1.0f);
    layer_data.mutable_outline_color()->set_g(0.8f);
    layer_data.mutable_outline_color()->set_b(0.8f);
    layer_data.mutable_outline_color()->set_a(1.0f);
    layer_data.set_outline_distance(1.0);
    layer_data.set_show_plane(false);

    // Clipping plane grid
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

} // namespace

ClippingPlaneLayerSystem* create_system()
{
    auto* sys = new ClippingPlaneLayerSystem();

    // This is the default plane written in the clipping planes array when we
    // don't have any other plane defined. It's configured as a pass-through so
    // that when someone uses an undefined plane, the geometry remains visible.
    {
        hrz_proto::GeographicPosition pos;
        pos.set_latitude(0);
        pos.set_longitude(0);
        pos.set_altitude(100000000);

        sys->default_plane.view = lm::inverse(hrz::ecef_to_enu_transform_for_geo(from_proto(pos)));
        sys->default_plane.normal = lm::dvec3(0, 0, -1);
        sys->default_plane.outline_color = lm::vec4(0.0);
        sys->default_plane.outline_distance = 1.0;
    }

    return sys;
}

void destroy_system(ClippingPlaneLayerSystem* sys, hrz::Render* render)
{
    sys->grid_renderable.deinit_rendering(render);
    delete sys;
}

void initialize_rendering(ClippingPlaneLayerSystem* system, hrz::Render* render)
{
    system->grid_renderable.init_rendering(render);
}

void register_layer(ClippingPlaneLayerSystem* system, SceneModel* model, uint64_t layer_id)
{
    assert(system && model);

    if (system->layer_ids_to_handles.count(layer_id) == 0)
    {
        auto handle = system->layer_pool.alloc();
        auto layer = system->layer_pool.get_object(handle);

        layer->clip_id = -1;

        system->layer_ids_to_handles.insert(std::make_pair(layer_id, handle));

        hrz_proto::PathRoot root;
        root.mutable_clipping_plane_layer()->set_opaque(layer_id);
        scene_model::register_element(model, root);

        hrz_proto::ClippingPlaneLayer layer_data = _default_layer_data(layer->clip_id);

        SceneModelAccessor accessor(model);
        hrz_proto::ClippingPlaneLayerPathBuilder<SceneModelAccessor> builder(
            accessor, root.clipping_plane_layer());
        builder.set(layer_data);
    }
}

void unregister_layer(ClippingPlaneLayerSystem* system, uint64_t layer_id)
{
    assert(system);
    system->unregistered_layers.insert(layer_id);
}

void notify_update(
    ClippingPlaneLayerSystem* system,
    uint64_t layer_id,
    scene_model::UpdateType,
    const scene_model::ClippingPlaneLayerPath& path)
{
    assert(system);

    auto layer = _get_layer(system, layer_id);
    if (!layer) return;

    layer->recompute_axis = path.leaf() || path.is_normal();

    system->updated_layers.insert(layer_id);
}

RenderRequest work(ClippingPlaneLayerSystem* system, SceneModel* model)
{
    RenderRequest render_request;

    render_request |= _unregister_layers(system, model);

    for (auto layer_id : system->updated_layers)
    {
        render_request |= _update_layer(system, model, layer_id);
    }
    system->updated_layers.clear();

    return render_request;
}

void draw(
    ClippingPlaneLayerSystem* system,
    hrz::Render* render,
    gsl::span<const hrz::RenderViewInfo> views_info)
{
    if (system->to_render.empty()) return;

    for (const auto& view : views_info)
    {
        uint32_t scene_view_bitset = (1U << (int)view.view);

        for (uint64_t layer_id : system->to_render)
        {
            auto layer = _get_layer(system, layer_id);
            if (!layer) continue;
            if ((layer->grid_params.scene_views_bitset & scene_view_bitset) == 0) continue;

            assert(layer->show_plane);

            const hrz::CameraViewInfo& view_info = view.cam_view_info;
            lm::dvec3 ecef_pos = layer->inv_view.w.xyz;
            lm::dvec3 offset_from_camera = ecef_pos - view_info.cam.pos;

            if (layer->recompute_axis)
            {
                const lm::dvec3 pos_normal = hrz::geo_to_normal(hrz::ecef_to_geo2(ecef_pos));
                const lm::dvec3 plane_normal =
                    lm::normalize((layer->inv_view * lm::dvec4(layer->normal, 0)).xyz);
                lm::vec3 axis_x, axis_y;

                if (1 - std::abs(lm::dot(pos_normal, plane_normal)) < 1e-4)
                {
                    axis_x = lm::vec3(layer->inv_view.x.xyz);
                    axis_y = lm::vec3(layer->inv_view.y.xyz);
                }
                else
                {
                    axis_x = lm::normalize(lm::vec3(lm::cross(plane_normal, pos_normal)));
                    axis_y = lm::normalize(lm::vec3(lm::cross(plane_normal, axis_x)));
                }

                layer->cached_axis_x = axis_x;
                layer->cached_axis_y = axis_y;
                layer->recompute_axis = false;
            }

            system->grid_renderable.schedule_draw(
                layer->grid_params, ecef_pos, offset_from_camera, layer->cached_axis_x,
                layer->cached_axis_y, {0, 0}, view.cam_view_info, scene_view_bitset);
        }
    }

    system->grid_renderable.draw(render);
}

void get_clip_planes_info(
    ClippingPlaneLayerSystem* system,
    ClippingPlaneInfo (&cpi)[HRZ_S_MAX_CLIP_PLANES])
{
    std::fill(std::begin(cpi), std::end(cpi), system->default_plane);

    for (uint64_t layer_id : system->all_active_planes)
    {
        const auto* layer = _get_layer(system, layer_id);

        if (_is_clip_plane_usable(layer))
        {
            auto& plane = cpi[layer->clip_id];
            plane.view = layer->view;
            plane.normal = layer->normal;
            plane.outline_color = layer->outline_color;
            plane.outline_distance = layer->outline_distance;

            // The computation of the clip distance divides by the outline
            // distance in the vertex shader. To avoid division by 0 or
            // other numerical instabilities, if the outline should be
            // invisible due to its width, we make it invisible due to its
            // colour instead.
            if (std::abs(plane.outline_distance) < 0.00001)
            {
                plane.outline_color = lm::vec4(0.0);
                plane.outline_distance = 1.0;
            }
        }
    }
}

} // namespace clipping_plane_layers
} // namespace hrz
