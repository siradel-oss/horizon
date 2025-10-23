#include "hrz_core_single_model_layers.h"

#include "assets_loader/hrz_core_assets_loader.h"
#include "hrz_core_loading_priorities.h"
#include "hrz_core_picking_id_allocator.h"
#include "hrz_core_scene.h"
#include "hrz_core_selection.h"
#include "hrz_core_visibility_constraints.h"
#include "model/hrz_core_model.h"
#include "model/hrz_core_model_materials_manager.h"
#include "planet/hrz_core_planet_surface.h"

#include <hrz_common_color.h>
#include <hrz_common_profiling.h>
#include <hrz_common_proto_geo.h>
#include <hrz_common_proto_maths.h>
#include <hrz_fnd_flat_hash_map.h>
#include <hrz_fnd_gen_object_pool.h>
#include <hrz_fnd_maths.h>
#include <hrz_protocol_path_builder.h>

namespace
{
struct ArraySyncTraits
{
    enum ElementUpdateType
    {
        All,
        Palette,
    };

    using ContainerPath = hrz::scene_model::SingleModelLayerPath;
    using ElementPath = hrz::scene_model::MaterialPath;

    static inline bool has_element_index(const ContainerPath& path)
    {
        return path.has_materials_index();
    }

    static inline size_t element_index(const ContainerPath& path) { return path.materials_index(); }

    static inline ElementPath element_path(const ContainerPath& path)
    {
        return path.clone().materials();
    }

    static inline ElementUpdateType element_update_type(const ElementPath& path)
    {
        if (path.is_data_texture_palette())
        {
            return ElementUpdateType::Palette;
        }
        else
        {
            return ElementUpdateType::All;
        }
    }
};

using ArraySync = hrz::scene_model::ArraySync<ArraySyncTraits>;

struct SingleModelLayer
{
    uint64_t global_layer_id;
    uint32_t local_layer_id;

    int8_t loading_priority;

    hrz::model::ModelPrototype* model_prototype = nullptr;
    hrz::model::SingleModelGeometryH model_geometry;

    ArraySync materials_array_sync;
    hrz::model::MaterialsManager<hrz::model::SingleModelMaterialManagerTraits> materials;

    hrz::model::DrawProperties draw_prps;

    bool is_visible;
    bool selected;
    hrz::layers::MultiviewVisibilityConstraints visibility_constraints_result;
    uint32_t displayable_in_scene_views;

    bool model_updated;
    bool transform_updated;
    bool visibility_updated;
    bool appearance_updated;
    bool overlay_material_properties_updated;
    bool active_materials_updated;
    bool loading_priority_updated;
    bool scene_views_updated;
    bool clip_id_updated;
    bool visibility_constraints_updated;
    bool http_headers_updated;

    hrz_proto::LayerVisibilityConstraintList visibility_constraints;

    lm::dmat4 transform; // COpy of what is in the scene model.
    lm::dvec3 anchor;    // In webmercator. z contains the fixed z offset from the scene model.

    bool clamping_enabled = true;
    bool needs_clamping = false;
    float clamping_z = 0.0f; // The terrain elevation at the clamping point.
    std::optional<hrz::planet::ElevationQueryTicket> elevation_query_ticket;

    constexpr bool should_work() const
    {
        return model_prototype != nullptr && is_visible
            && (displayable_in_scene_views & visibility_constraints_result.satisfied_in) != 0;
    }
};
} // namespace

namespace hrz
{
struct SingleModelLayerSystem
{
    using IndexPool = GenIndexPool<uint64_t, 32, 32>;
    using LayerPool = GenObjectPool<SingleModelLayer, IndexPool, 64>;

    LayerPool layer_pool;
    hrz::flat_hash_map<uint64_t, uint64_t> layer_ids_to_handles;

    hrz::flat_hash_set<uint64_t> updated_layers;
    hrz::flat_hash_set<uint64_t> unregistered_layers;

    uint8_t picking_id;
    GenIndexPool<uint32_t, 4, 20> local_layer_id_pool;
    hrz::flat_hash_map<uint32_t, uint64_t> local_to_global_layer_id;

    model::SharedResources* shared_resources;
    std::vector<my::ResourceHandle> to_destroy;

    uint64_t last_terrain_version = UINT64_MAX;
};

namespace single_model_layers
{
namespace
{
void _delete_model_prototype(
    SingleModelLayerSystem* system,
    SingleModelLayer* layer,
    AssetsLoader* assets_loader,
    JobScheduler* job_scheduler,
    BlobAllocator* blob_allocator)
{
    if (layer->model_prototype != nullptr)
    {
        layer->materials.delete_all(layer->model_prototype);

        model::destroy(layer->model_prototype, layer->model_geometry);

        model::destroy(
            layer->model_prototype, assets_loader, job_scheduler, blob_allocator,
            system->to_destroy);

        layer->model_prototype = nullptr;
    }
}

SingleModelLayer* _get_layer(SingleModelLayerSystem* system, uint64_t layer_id)
{
    auto it = system->layer_ids_to_handles.find(layer_id);
    if (it == system->layer_ids_to_handles.end()) return nullptr;

    return system->layer_pool.get_object(it->second);
}

void _update_transform(SingleModelLayer* layer)
{
    auto geo_anchor = web_mercator_to_geo3(layer->anchor);
    geo_anchor.alt += layer->clamping_enabled ? layer->clamping_z : 0.0f;

    auto geo_matrix = enu_to_ecef_transform_for_geo(geo_anchor);
    layer->draw_prps.transform = geo_matrix * layer->transform;
}

RenderRequest _update_layer(
    SingleModelLayerSystem* system,
    SceneModel* model,
    uint64_t layer_id,
    AssetsLoader* assets_loader,
    JobScheduler* job_scheduler,
    BlobAllocator* blob_allocator,
    AttributionRegistry* attributions)
{
    HRZ_SCOPED_SAMPLE("update single model layer");

    RenderRequest render_request;

    auto layer = _get_layer(system, layer_id);
    if (!layer) return render_request;

    SceneModelAccessor accessor(model);

    hrz_proto::LayerHandle handle;
    handle.set_opaque(layer_id);

    hrz_proto::SingleModelLayerPathBuilder<SceneModelAccessor> builder(accessor, handle);

    bool force_resynchronize_all_materials = false;

    if (layer->loading_priority_updated)
    {
        layer->loading_priority =
            clamp_cast<int32_t, uint8_t>(builder.clone().loading_priority().get());
        layer->loading_priority_updated = false;
    }

    if (layer->http_headers_updated)
    {
        if (layer->model_prototype)
        {
            auto http_headers =
                hrz::assets_loader::from_proto(builder.clone().http_headers().get());
            if (model::update_http_headers(layer->model_prototype, http_headers))
            {
                layer->model_updated = true;
            }
        }
        layer->http_headers_updated = false;
    }

    if (layer->model_updated)
    {
        _delete_model_prototype(system, layer, assets_loader, job_scheduler, blob_allocator);

        auto new_model_url = builder.clone().url().get();
        if (!new_model_url.empty())
        {
            auto http_headers =
                hrz::assets_loader::from_proto(builder.clone().http_headers().get());

            auto new_attribution = attribution::register_attribution(
                attributions, {builder.clone().attribution().get(), ""});

            auto preserve_query_parameters = builder.clone().preserve_query_parameters().get();

            layer->model_prototype = model::create_from_gltf_url(
                assets_loader, {new_model_url, preserve_query_parameters}, http_headers,
                new_attribution,
                get_request_queue(layer->loading_priority, assets_loader::Queue::MeshModels),
                combine_loading_priorities(layer->loading_priority, 0),
                {monitoring::systems::SingleModelLayers, layer->global_layer_id});

            layer->model_geometry = model::create_single_model_geometry(
                layer->model_prototype,
                picking::ObjectReference{system->picking_id, layer->local_layer_id, 0},
                picking::FeatureReference{system->picking_id, layer->local_layer_id, 0});
        }

        layer->materials.prototype_may_have_been_recreated(layer->model_prototype);

        force_resynchronize_all_materials = true;
        layer->model_updated = false;
        render_request.request_visual_render();
    }

    auto recreate_all_materials_fn = [&]()
    {
        auto layer_proto = builder.clone().get();
        layer->materials.recreate_all_materials(
            layer->model_prototype,
            std::span<const hrz_proto::Material* const>(
                layer_proto.materials().data(), (size_t)layer_proto.materials().size()));
    };

    if (force_resynchronize_all_materials && layer->model_prototype)
    {
        recreate_all_materials_fn();
        layer->materials_array_sync.reset(layer->materials.material_count());
    }
    else if (layer->model_prototype)
    {
        layer->materials_array_sync.synchronize(
            [&](const ArraySync::Command& cmd) -> size_t
            {
                switch (cmd.type)
                {
                    case ArraySync::Command::RebuildAll:
                    {
                        recreate_all_materials_fn();
                        break;
                    }
                    case ArraySync::Command::Add:
                    {
                        layer->materials.add_material(
                            builder.clone().materials(cmd.info.add.index_auth).get());
                        break;
                    }
                    case ArraySync::Command::Update:
                    {
                        switch (cmd.info.update.update_type)
                        {
                            case ArraySyncTraits::ElementUpdateType::All:
                                layer->materials.update_whole_material(
                                    layer->model_prototype,
                                    builder.clone().materials(cmd.info.update.index_auth).get(),
                                    cmd.info.update.index_mirror);
                                break;
                            case ArraySyncTraits::ElementUpdateType::Palette:
                                layer->materials.update_material_palette(
                                    layer->model_prototype,
                                    builder.clone()
                                        .materials(cmd.info.update.index_auth)
                                        .data_texture_palette()
                                        .get(),
                                    cmd.info.update.index_mirror);
                                break;
                        }
                        break;
                    }
                    case ArraySync::Command::Remove:
                    {
                        layer->materials.remove_material(
                            layer->model_prototype, cmd.info.remove.index_mirror);
                        break;
                    }
                }
                return layer->materials.material_count();
            });
    }

    if (layer->active_materials_updated)
    {
        auto prps = builder.clone().material_properties().get();
        layer->materials.set_active_materials(
            prps.base_material(),
            prps.enable_overlay() ? prps.overlay_material() : std::optional<std::string_view>());
        layer->active_materials_updated = false;
    }

    render_request |= layer->materials.update(layer->model_prototype, layer->model_geometry);

    if (layer->overlay_material_properties_updated)
    {
        auto prps = builder.clone().material_properties().get();
        layer->draw_prps.overlay_material_enabled = prps.enable_overlay();
        layer->draw_prps.overlay_material_opacity = prps.overlay_opacity();
        layer->draw_prps.apply_feature_color_to_overlay = prps.apply_feature_color_to_overlay();
        layer->draw_prps.feature_color_blend_mode = prps.feature_color_blend_mode();
        layer->draw_prps.feature_color_blend_strength = prps.feature_color_blend_strength();
        layer->overlay_material_properties_updated = false;
        render_request.request_visual_render();
    }

    if (layer->transform_updated)
    {
        layer->transform = to_lm(builder.clone().transform().get());
        layer->anchor = geo_to_web_mercator(from_proto(builder.clone().geographic().get()));
        layer->clamping_enabled = builder.clone().clamp_to_ground().get();
        layer->needs_clamping = layer->clamping_enabled;
        if (!layer->clamping_enabled)
        {
            layer->clamping_z = 0.0f;
        }
        _update_transform(layer);
        layer->transform_updated = false;
        render_request.request_visual_render();
    }

    if (layer->visibility_updated)
    {
        layer->is_visible = builder.clone().visible().get();
        layer->visibility_updated = false;
        render_request.request_visual_render();
    }

    if (layer->appearance_updated)
    {
        layer->draw_prps.lighting = hrz::render::from_proto(builder.clone().lighting().get());
        layer->draw_prps.color =
            hrz::srgb_to_linear(hrz::convert_proto_color_to_float(builder.clone().color().get()));

        layer->appearance_updated = false;
        render_request.request_visual_render();
    }

    if (layer->scene_views_updated)
    {
        layer->displayable_in_scene_views = builder.clone().scene_views().bits().get();
        layer->scene_views_updated = false;
        render_request.request_visual_render();
    }

    if (layer->visibility_constraints_updated)
    {
        layer->visibility_constraints = builder.clone().visibility_constraints().get();
        layer->visibility_constraints_updated = false;
        render_request.request_visual_render();
    }

    if (layer->clip_id_updated)
    {
        layer->draw_prps.clip_id = builder.clone().clip_id().get();
        layer->clip_id_updated = false;
        render_request.request_visual_render();
    }

    return render_request;
}
} // namespace

SingleModelLayerSystem* create_system(PickingIdAllocator* picking_id_allocator)
{
    auto system = new SingleModelLayerSystem();
    system->picking_id = picking::allocate_system_id(picking_id_allocator);
    system->shared_resources = nullptr;

    return system;
}

void initialize_rendering(SingleModelLayerSystem* system, Render* render)
{
    assert(system && render);
    assert(system->shared_resources == nullptr);

    system->shared_resources = model::create_shared_resources_single(render);
}

void notify_model_update(
    SingleModelLayerSystem* system,
    uint64_t layer_id,
    scene_model::UpdateType update_type,
    const scene_model::SingleModelLayerPath& path)
{
    assert(system);

    auto layer = _get_layer(system, layer_id);
    if (!layer) return;

    if (path.is_url() || path.is_preserve_query_parameters() || path.is_attribution()
        || path.leaf())
    {
        layer->model_updated = true;
    }

    if (path.is_http_headers() || path.leaf())
    {
        layer->http_headers_updated = true;
    }

    if (path.is_transform() || path.is_geographic() || path.leaf())
    {
        layer->transform_updated = true;
    }

    if (path.is_visible() || path.leaf())
    {
        layer->visibility_updated = true;
    }

    if (path.is_color() || path.is_lighting() || path.leaf())
    {
        layer->appearance_updated = true;
    }

    if (path.is_loading_priority() || path.leaf())
    {
        layer->loading_priority_updated = true;
    }

    if (path.is_visibility_constraints() || path.leaf())
    {
        layer->visibility_constraints_updated = true;
    }

    if (path.is_scene_views() || path.leaf())
    {
        layer->scene_views_updated = true;
    }

    if (path.is_clip_id() || path.leaf())
    {
        layer->clip_id_updated = true;
    }

    if (path.is_material_properties() || path.leaf())
    {
        layer->overlay_material_properties_updated = true;
    }

    if (path.leaf() || path.is_materials())
    {
        layer->materials_array_sync.notify_model_update(update_type, path);
    }

    if (path.is_material_properties())
    {
        auto prps_path = path.clone().material_properties();
        if (prps_path.leaf() || prps_path.is_base_material() || prps_path.is_overlay_material()
            || prps_path.is_enable_overlay())
        {
            layer->active_materials_updated = true;
        }
    }
    else if (path.leaf())
    {
        layer->active_materials_updated = true;
    }

    system->updated_layers.insert(layer_id);
}

void register_layer(SingleModelLayerSystem* system, SceneModel* model, uint64_t layer_id)
{
    assert(system && model);

    if (system->layer_ids_to_handles.count(layer_id) == 0)
    {
        auto handle = system->layer_pool.alloc();
        auto layer = system->layer_pool.get_object(handle);
        layer->global_layer_id = layer_id;

        layer->local_layer_id = system->local_layer_id_pool.alloc();
        system->local_to_global_layer_id.insert({layer->local_layer_id, layer_id});

        layer->model_prototype = nullptr;
        layer->visibility_constraints = hrz_proto::LayerVisibilityConstraintList();

        system->layer_ids_to_handles.insert(std::make_pair(layer_id, handle));

        hrz_proto::PathRoot root;
        root.mutable_single_model_layer()->set_opaque(layer_id);
        scene_model::register_element(model, root);

        // Default data
        hrz_proto::SingleModelLayer data;
        data.mutable_transform()->mutable_rotation()->set_w(1);

        auto scale = data.mutable_transform()->mutable_scale();
        scale->set_x(1);
        scale->set_y(1);
        scale->set_z(1);

        auto frame = data.mutable_transform()->mutable_frame();
        frame->set_up(HrzProtocol::Axis::POS_Z);
        frame->set_front(HrzProtocol::Axis::POS_Y);
        frame->set_handedness(HrzProtocol::Handedness::RIGHT);

        layer->is_visible = true;
        data.set_visible(true);

        layer->displayable_in_scene_views = (1 << SCENE_VIEW_COUNT) - 1;
        data.mutable_scene_views()->set_bits(layer->displayable_in_scene_views);

        data.mutable_color()->set_r(1.0f);
        data.mutable_color()->set_g(1.0f);
        data.mutable_color()->set_b(1.0f);
        data.mutable_color()->set_a(1.0f);

        data.mutable_material_properties()->set_feature_color_blend_mode(hrz_proto::BLEND_MULTIPLY);
        data.mutable_material_properties()->set_feature_color_blend_strength(1.0f);

        data.set_clip_id(-1);

        data.mutable_lighting()->set_enable_lighting(true);
        data.mutable_lighting()->set_cast_shadows(true);
        data.mutable_lighting()->set_receive_shadows(true);

        data.mutable_material_properties()->set_overlay_opacity(1.0f);
        data.mutable_material_properties()->set_apply_feature_color_to_overlay(true);

        SceneModelAccessor accessor(model);
        hrz_proto::SingleModelLayerPathBuilder<SceneModelAccessor> builder(
            accessor, root.single_model_layer());
        builder.set(data);

        layer->transform_updated = true;
        layer->clip_id_updated = true;
        layer->appearance_updated = true;
    }
}

void unregister_layer(SingleModelLayerSystem* system, uint64_t layer_id)
{
    assert(system);
    system->unregistered_layers.insert(layer_id);
}

RenderRequest _unregister_layers(
    SingleModelLayerSystem* system,
    AssetsLoader* al,
    JobScheduler* js,
    BlobAllocator* ba,
    SceneModel* model,
    PlanetSurface* planet)
{
    assert(system && al && js && ba && model && planet);

    RenderRequest render_request;

    for (auto layer_id : system->unregistered_layers)
    {
        auto it = system->layer_ids_to_handles.find(layer_id);
        if (it != system->layer_ids_to_handles.end())
        {
            hrz_proto::PathRoot root;
            root.mutable_single_model_layer()->set_opaque(layer_id);
            scene_model::unregister_element(model, root);

            auto inner_id = it->second;
            auto layer = system->layer_pool.get_object(inner_id);

            if (layer)
            {
                _delete_model_prototype(system, layer, al, js, ba);

                if (layer->elevation_query_ticket.has_value())
                {
                    planet::cancel_elevation_query(planet, layer->elevation_query_ticket.value());
                }

                system->local_layer_id_pool.release(layer->local_layer_id);
                system->layer_pool.release(inner_id);

                render_request.request_visual_render();
            }

            system->layer_ids_to_handles.erase(it);
        }
    }
    system->unregistered_layers.clear();

    return render_request;
}

void destroy_system(
    SingleModelLayerSystem* system,
    Render* render,
    AssetsLoader* assets_loader,
    JobScheduler* job_scheduler,
    BlobAllocator* blob_allocator,
    PickingIdAllocator* id_allocator,
    SceneModel* model,
    PlanetSurface* planet)
{
    assert(system);

    for (auto& pair : system->layer_ids_to_handles)
    {
        unregister_layer(system, pair.first);
    }
    _unregister_layers(system, assets_loader, job_scheduler, blob_allocator, model, planet);

    if (system->shared_resources != nullptr)
    {
        model::destroy_shared_resources(system->shared_resources, render);
    }

    picking::release_system_id(id_allocator, system->picking_id);

    delete system;
}

RenderRequest _work_models(
    SingleModelLayerSystem* system,
    AssetsLoader* assets_loader,
    JobScheduler* job_scheduler,
    BlobAllocator* blob_allocator,
    ImageDecoder* image_decoder,
    AttributionRegistry* attributions,
    PlanetSurface* planet,
    bool terrain_version_changed)
{
    RenderRequest render_request;

    for (auto& pair : system->layer_ids_to_handles)
    {
        auto layer = system->layer_pool.get_object(pair.second);

        if (layer->model_prototype && layer->should_work())
        {
            layer->materials.work(layer->model_prototype);
            model::work(
                layer->model_prototype, assets_loader, job_scheduler, blob_allocator, image_decoder,
                attributions);
        }

        layer->needs_clamping =
            layer->needs_clamping || (layer->clamping_enabled && terrain_version_changed);

        if (!layer->clamping_enabled && layer->elevation_query_ticket.has_value())
        {
            planet::cancel_elevation_query(planet, layer->elevation_query_ticket.value());
            layer->elevation_query_ticket.reset();
        }
        else if (layer->clamping_enabled)
        {
            if (layer->needs_clamping && layer->elevation_query_ticket.has_value())
            {
                planet::cancel_elevation_query(planet, layer->elevation_query_ticket.value());
                layer->elevation_query_ticket.reset();
            }

            if (layer->needs_clamping)
            {
                assert(!layer->elevation_query_ticket.has_value());
                auto ticket = planet::query_elevation(
                    planet, layer->anchor.xy,
                    monitoring::ResourceOwner{
                        monitoring::systems::SingleModelLayers, layer->global_layer_id});
                layer->elevation_query_ticket = ticket;
                layer->needs_clamping = false;
            }

            if (layer->elevation_query_ticket.has_value()
                && planet::is_elevation_query_ready(planet, *layer->elevation_query_ticket))
            {
                auto result =
                    planet::retrieve_elevation_query(planet, layer->elevation_query_ticket.value());
                layer->elevation_query_ticket.reset();

                if (result && result->size() > 0)
                {
                    auto array = result->get_cdata();
                    layer->clamping_z = array[0];
                }
                else
                {
                    layer->clamping_z = 0.0f;
                }
                _update_transform(layer);
                render_request.request_visual_render();
            }
        }
    }

    return render_request;
}

static SingleModelLayer* _get_layer_from_picking(
    SingleModelLayerSystem* system,
    const picking::ObjectReference& ref)
{
    if (ref.system_id != system->picking_id) return nullptr;

    auto layer_local_id = ref.complementary_id;
    auto it = system->local_to_global_layer_id.find(layer_local_id);

    if (it == system->local_to_global_layer_id.end())
    {
        HRZ_LOG_ERROR("Cannot find single model layer for picking id {}", layer_local_id);
        return nullptr;
    }

    auto handle = system->layer_ids_to_handles.at(it->second);
    return system->layer_pool.get_object(handle);
}

void pick(
    SingleModelLayerSystem* system,
    const picking::PositionResult& result_raw,
    hrz_proto::PickResults& picking_results)
{
    HRZ_SCOPED_SAMPLE("single model layers notify picking");

    auto layer = _get_layer_from_picking(system, result_raw.ref);

    if (layer && layer->is_visible && layer->visibility_constraints_result.satisfied_in != 0)
    {
        hrz_proto::PickLayerResult* pr = picking_results.mutable_results()->Add();
        pr->mutable_layer()->set_type(hrz_proto::LayerType::SINGLE_MODEL);
        pr->mutable_layer()->mutable_handle()->set_opaque(layer->global_layer_id);
        pr->mutable_model()->set_data_texture_value(result_raw.data_texture_value);
    }
}

std::pair<size_t, size_t> make_typed_object_references(
    SingleModelLayerSystem* system,
    std::span<const picking::ObjectReference> objs,
    std::span<hrz_proto::TypedObjectReference> output)
{
    assert(objs.size() <= output.size());

    size_t in_cursor = 0;
    size_t out_cursor = 0;

    uint32_t last_complementary_id = 0;
    const SingleModelLayer* layer = nullptr;

    // We assume that objs are sorted, thus if we find an object reference
    // without the correct system id, we assume there are no more inputs with
    // the same system id.
    while (in_cursor < objs.size() && objs[in_cursor].system_id == system->picking_id)
    {
        const auto& obj = objs[in_cursor++];
        if (!layer || last_complementary_id != obj.complementary_id)
        {
            layer = _get_layer_from_picking(system, obj);
            last_complementary_id = obj.complementary_id;

            // Since there is no object ids for single models, it's fine to only
            // insert in the output list once per complementary_id contiguous
            // sequence since the input references are assumed to be sorted.
            if (layer && layer->is_visible
                && layer->visibility_constraints_result.satisfied_in != 0)
            {
                auto& typed_obj = output[out_cursor++];
                typed_obj.mutable_single_model()->set_opaque(layer->global_layer_id);
            }
        }
    }

    return std::make_pair(in_cursor, out_cursor);
}

std::optional<picking::FeatureReference> make_feature_reference(
    SingleModelLayerSystem* system,
    const picking::ObjectReference& obj)
{
    assert(system);

    if (obj.system_id != system->picking_id)
    {
        return std::nullopt;
    }

    auto layer = _get_layer_from_picking(system, obj);
    if (layer == nullptr)
    {
        return std::nullopt;
    }

    picking::FeatureReference ref;
    ref.feature_id_hash = 0;
    ref.complementary_id = obj.complementary_id;
    ref.system_id = obj.system_id;

    return ref;
}

RenderRequest work(
    SingleModelLayerSystem* system,
    SceneModel* model,
    AssetsLoader* assets_loader,
    JobScheduler* job_scheduler,
    BlobAllocator* blob_allocator,
    ImageDecoder* image_decoder,
    const SelectionSystem* selection,
    AttributionRegistry* attributions,
    PlanetSurface* planet,
    std::span<const RenderViewInfo> views_info)
{
    HRZ_SCOPED_SAMPLE("single model layers work");

    assert(
        system && model && assets_loader && job_scheduler && blob_allocator && image_decoder
        && selection);

    RenderRequest render_request;

    render_request |=
        _unregister_layers(system, assets_loader, job_scheduler, blob_allocator, model, planet);

    uint64_t new_terrain_version = planet::get_terrain_version(planet);
    bool terrain_version_changed =
        new_terrain_version != std::exchange(system->last_terrain_version, new_terrain_version);

    for (auto layer_id : system->updated_layers)
    {
        render_request |= _update_layer(
            system, model, layer_id, assets_loader, job_scheduler, blob_allocator, attributions);
    }
    system->updated_layers.clear();

    for (auto& pair : system->layer_ids_to_handles)
    {
        auto layer = system->layer_pool.get_object(pair.second);

        auto new_result =
            layers::are_visibility_constraints_satisfied(views_info, layer->visibility_constraints);
        if (new_result != layer->visibility_constraints_result)
        {
            layer->visibility_constraints_result = new_result;
            render_request.request_visual_render();
        }
    }

    render_request |= _work_models(
        system, assets_loader, job_scheduler, blob_allocator, image_decoder, attributions, planet,
        terrain_version_changed);

    if (selection::has_changed_since_last_frame(selection))
    {
        for (const auto& it : system->layer_ids_to_handles)
        {
            uint64_t layer_id = it.first;
            uint64_t layer_handle = it.second;

            bool selected = selection::selected_objects_count(selection, layer_id) > 0;

            SingleModelLayer* layer = system->layer_pool.get_object(layer_handle);
            assert(layer);

            layer->selected = selected;
            render_request.request_visual_render();
        }
    }

    return render_request;
}

bool is_working(SingleModelLayerSystem* system)
{
    assert(system);
    for (auto& pair : system->layer_ids_to_handles)
    {
        auto layer = system->layer_pool.get_object(pair.second);

        if (layer->model_prototype && layer->should_work())
        {
            if (model::is_working(layer->model_prototype)
                || layer->materials.is_working(layer->model_prototype))
            {
                return true;
            }
        }
    }
    return false;
}

RenderRequest work_gpu(SingleModelLayerSystem* system, Render* render, BlobAllocator* ba)
{
    assert(system && render);
    assert(system->shared_resources != nullptr);

    RenderRequest render_request;

    for (my::ResourceHandle res : system->to_destroy)
    {
        render->rc->dealloc(res);
    }
    system->to_destroy.clear();

    for (auto& pair : system->layer_ids_to_handles)
    {
        auto layer = system->layer_pool.get_object(pair.second);

        if (layer && layer->should_work())
        {
            model::work_gpu(layer->model_prototype, ba, render);
            render_request |=
                layer->materials.work_gpu(layer->model_prototype, system->shared_resources, render);
        }
    }

    return render_request;
}

void draw(SingleModelLayerSystem* system, Render* render, AttributionRegistry* attributions)
{
    assert(system && render);
    assert(system->shared_resources != nullptr);

    for (auto& pair : system->layer_ids_to_handles)
    {
        auto layer = system->layer_pool.get_object(pair.second);

        if (layer && layer->should_work())
        {
            uint32_t display_in = layer->displayable_in_scene_views
                & layer->visibility_constraints_result.satisfied_in;

            auto active_model = layer->materials.get_active_model();
            if (display_in && active_model.has_value())
            {
                model::draw(
                    layer->model_prototype, active_model.value(), layer->draw_prps, layer->selected,
                    display_in, system->shared_resources, render, attributions);
            }
        }
    }
}

} // namespace single_model_layers

} // namespace hrz
