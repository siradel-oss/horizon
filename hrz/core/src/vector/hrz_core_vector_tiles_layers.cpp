#include "vector/hrz_core_vector_tiles_layers.h"

#include "hrz_core_picking_id_allocator.h"
#include "hrz_core_scene.h"
#include "hrz_core_scene_model.h"
#include "hrz_core_scene_model_array_sync.h"
#include "hrz_core_scene_path.h"
#include "hrz_core_selection.h"
#include "hrz_core_visibility_constraints.h"
#include "vector/hrz_core_vector_heatmaps.h"
#include "vector/hrz_core_vector_image_loader.h"
#include "vector/hrz_core_vector_repr.h"
#include "vector/hrz_core_vector_tiles.h"

#include <hrz_common_layers.h>
#include <hrz_common_profiling.h>
#include <hrz_common_proto_geo.h>
#include <hrz_common_proto_maths.h>
#include <hrz_common_vector_tiles.h>
#include <hrz_core_render.h>
#include <hrz_fnd_flat_hash_map.h>
#include <hrz_fnd_flat_hash_set.h>
#include <hrz_fnd_format.h>
#include <hrz_fnd_gen_object_pool.h>
#include <hrz_fnd_log.h>
#include <hrz_fnd_maths.h>
#include <hrz_protocol_path_builder.h>

#include <optional>

extern "C"
{
#include <microui/microui.h>
}

namespace
{
struct ArraySyncTraits
{
    enum ElementUpdateType
    {
        All,
    };

    using ContainerPath = hrz::scene_model::VectorTilesStylePath;
    using ElementPath = hrz::scene_model::VectorReprPath;

    static inline bool has_element_index(const ContainerPath& path)
    {
        return path.has_representations_index();
    }

    static inline size_t element_index(const ContainerPath& path)
    {
        return path.representations_index();
    }

    static inline ElementPath element_path(const ContainerPath& path)
    {
        return path.clone().representations();
    }

    static inline ElementUpdateType element_update_type(const ElementPath& path)
    {
        return ElementUpdateType::All;
    }
};

struct Layer
{
    uint64_t id;
    uint32_t picking_id;

    uint32_t vector_data_layer;
    bool static_tiles;
    hrz_proto::MissingTilePolicy missing_tile_policy;
    hrz::vt::VectorTiles* vt;
    bool is_visible{};
    hrz::SceneViewBitset scene_views;
    hrz::layers::MultiviewVisibilityConstraints visibility_constraints_result;

    inline hrz::SceneViewBitset visible_in() const
    {
        return hrz::SceneViewBitset(is_visible) & scene_views
            & hrz::SceneViewBitset(
                   visibility_constraints_result.satisfied_in
                   & visibility_constraints_result.active_views);
    }

    bool layer_updated;
    bool visibility_updated;
    bool bounds_updated;
    bool resolution_updated;
    bool clamping_updated;
    bool style_script_updated;
    bool special_attributes_updated;
    bool attributes_updated;
    bool palettes_updated;
    bool rng_seed_updated;
    bool appearance_updated;
    bool visibility_constraints_updated;

    hrz_proto::LayerVisibilityConstraintList visibility_constraints;

    using ArraySync = hrz::scene_model::ArraySync<ArraySyncTraits>;
    size_t reprs_count = 0;
    ArraySync reprs_array_sync;
};

} // namespace

namespace hrz
{
struct VectorTilesLayerSystem
{
    using IndexPool = GenIndexPool<uint64_t, 32, 32>;
    using LayerPool = GenObjectPool<Layer, IndexPool, 64>;

    LayerPool layer_pool;
    hrz::flat_hash_map<uint64_t, uint64_t> layer_ids_to_handles;

    std::vector<uint64_t> unregistered_layers;

    uint8_t picking_id;
    hrz::GenIndexPool<uint32_t, 2, 12> picking_id_pool;
    hrz::flat_hash_map<uint32_t, uint64_t> picking_id_to_layer_ids;

    bool has_render_been_initialized = false;
    hrz::vt::ReprRegistry reprs;
    hrz::HeatmapReprRegistry* heatmap_repr_registry;
    hrz::vt::ImageLoader* image_loader;
};

namespace vector_tiles_layers
{
namespace
{
Layer* _get_layer(VectorTilesLayerSystem* system, uint64_t layer_id)
{
    auto it = system->layer_ids_to_handles.find(layer_id);
    if (it == system->layer_ids_to_handles.end())
    {
        return nullptr;
    }
    return system->layer_pool.get_object(it->second);
}

RenderRequest _unregister_layers(
    VectorTilesLayerSystem* system,
    SceneModel* model,
    BlobAllocator* ba,
    JobScheduler* js,
    hrz::PlanetSurface* planet)
{
    assert(system && model && js);

    RenderRequest render_request;

    for (auto layer_id : system->unregistered_layers)
    {
        auto it = system->layer_ids_to_handles.find(layer_id);
        if (it != system->layer_ids_to_handles.end())
        {
            hrz_proto::PathRoot root;
            root.mutable_vector_tiles_layer()->set_opaque(layer_id);
            scene_model::unregister_element(model, root);

            auto layer_handle = it->second;
            auto layer = system->layer_pool.get_object(layer_handle);

            if (layer)
            {
                vt::destroy(layer->vt);

                system->picking_id_pool.release(
                    picking::extract_complementary_id(layer->picking_id));

                system->layer_pool.release(layer_handle);
            }

            system->layer_ids_to_handles.erase(it);

            render_request.request_visual_render();
            render_request.schedule_flat_overlay_render();
        }
    }

    system->unregistered_layers.clear();

    return render_request;
}

RenderRequest _update_layer(
    VectorTilesLayerSystem* system,
    SceneModel* model,
    uint64_t layer_id,
    VectorDataLoader* vdl,
    BlobAllocator* ba,
    JobScheduler* js,
    PlanetSurface* planet,
    ActorRunner* ar)
{
    HRZ_SCOPED_SAMPLE("update vector tiles layer");

    RenderRequest render_request;

    auto layer = _get_layer(system, layer_id);
    if (!layer) return render_request;

    hrz::SceneModelAccessor accessor(model);

    hrz_proto::LayerHandle layer_handle;
    layer_handle.set_opaque(layer_id);

    hrz_proto::VectorTilesLayerPathBuilder<hrz::SceneModelAccessor> builder(accessor, layer_handle);

    if (layer->layer_updated)
    {
        auto new_data = builder.clone().get();
        auto vector_data_layer_id = new_data.source().vector_data_layer_id();
        auto missing_tile_policy = new_data.missing_tile_policy();
        auto static_tiles = new_data.static_tiles();

        vt::destroy(layer->vt);

        layer->vt = vt::create(
            layer_id, vector_data_layer_id, missing_tile_policy, static_tiles, layer->picking_id,
            planet, vdl, ar);
        vt::set_visible_in(layer->vt, layer->visible_in());
    }

    if (layer->layer_updated || layer->bounds_updated)
    {
        auto new_data = builder.clone().source().get();
        vt::set_bounds(
            layer->vt,
            new_data.override_bounds()
                ? std::optional<hrz::GeoBounds>{hrz::from_proto(new_data.bounds())}
                : std::nullopt,
            new_data.override_levels() ? std::optional<uint8_t>{new_data.min_level()}
                                       : std::nullopt,
            new_data.override_levels() ? std::optional<uint8_t>{new_data.max_level()}
                                       : std::nullopt);
    }

    if (layer->layer_updated || layer->resolution_updated)
    {
        auto new_data = builder.clone().resolution().get();
        vt::set_max_screen_space_error(layer->vt, new_data.max_screen_space_error());
    }

    if (layer->layer_updated || layer->clamping_updated)
    {
        auto new_data = builder.clone().clamping().get();
        vt::set_clamping(layer->vt, new_data);
    }

    if (layer->layer_updated || layer->attributes_updated)
    {
        const auto& new_style = builder.clone().style().get();
        vt::set_attributes(
            layer->vt, {new_style.attributes().data(), (size_t)new_style.attributes_size()});
    }

    if (layer->layer_updated || layer->palettes_updated)
    {
        const auto& new_style = builder.clone().style().get();
        vt::set_palettes(
            layer->vt, {new_style.palettes().data(), (size_t)new_style.palettes_size()});
    }

    if (layer->layer_updated || layer->rng_seed_updated)
    {
        uint64_t rng_seed = builder.clone().style().rng_seed().get();
        vt::set_rng_seed(layer->vt, rng_seed);
    }

    if (layer->layer_updated || layer->style_script_updated)
    {
        const auto& new_script = builder.clone().style().styling_script().get();
        vt::set_style_script(layer->vt, new_script);
    }

    if (layer->layer_updated || layer->special_attributes_updated)
    {
        const auto& new_anchor_z_attribute_name =
            builder.clone().style().anchor_z_attribute_name().get();

        const auto& new_anchor_angle_attribute_name =
            builder.clone().style().anchor_angle_attribute_name().get();

        const auto& new_feature_type_attribute_name =
            builder.clone().style().feature_type_attribute_name().get();

        vt::set_special_attributes(
            layer->vt, new_anchor_z_attribute_name, new_anchor_angle_attribute_name,
            new_feature_type_attribute_name);
    }

    if (layer->layer_updated)
    {
        layer->reprs_array_sync.notify_model_update_all();
    }

    layer->reprs_array_sync.synchronize(
        [&](const Layer::ArraySync::Command& cmd) -> size_t
        {
            using Command = Layer::ArraySync::Command;
            switch (cmd.type)
            {
                case Command::RebuildAll:
                {
                    const auto& new_style = builder.clone().style().get();
                    vt::set_all_representations(
                        layer->vt, &system->reprs,
                        {new_style.representations().data(),
                         (size_t)new_style.representations_size()});
                    layer->reprs_count = new_style.representations_size();
                    break;
                }
                case Command::Add:
                {
                    auto repr = builder.clone()
                                    .style()
                                    .representations((uint32_t)cmd.info.add.index_auth)
                                    .get();
                    vt::add_representation(layer->vt, &system->reprs, std::move(repr));
                    layer->reprs_count += 1;
                    break;
                }
                case Command::Update:
                {
                    auto repr = builder.clone()
                                    .style()
                                    .representations((uint32_t)cmd.info.update.index_auth)
                                    .get();
                    vt::update_representation(
                        layer->vt, &system->reprs, cmd.info.update.index_mirror, std::move(repr));
                    break;
                }
                case Command::Remove:
                {
                    vt::remove_representation(
                        layer->vt, &system->reprs, cmd.info.remove.index_mirror);
                    layer->reprs_count -= 1;
                    break;
                }
            }
            return layer->reprs_count;
        });

    if (layer->layer_updated || layer->visibility_updated)
    {
        layer->is_visible = builder.clone().visible().get();
        layer->scene_views = builder.clone().scene_views().bits().get();
        vt::set_visible_in(layer->vt, layer->visible_in());
        render_request.request_visual_render();
        render_request.schedule_flat_overlay_render();
    }

    if (layer->layer_updated || layer->visibility_constraints_updated)
    {
        layer->visibility_constraints = builder.clone().visibility_constraints().get();
        render_request.request_visual_render();
        render_request.schedule_flat_overlay_render();
    }

    if (layer->layer_updated || layer->appearance_updated)
    {
        int8_t clip_id = hrz::clamp_cast<int32_t, int8_t>(builder.clone().clip_id().get());
        auto lighting_settings = render::from_proto(builder.clone().lighting().get());
        vt::set_clip_id(layer->vt, clip_id);
        vt::set_lighting(layer->vt, lighting_settings);
    }

    layer->layer_updated = false;
    layer->bounds_updated = false;
    layer->clamping_updated = false;
    layer->attributes_updated = false;
    layer->palettes_updated = false;
    layer->rng_seed_updated = false;
    layer->style_script_updated = false;
    layer->special_attributes_updated = false;
    layer->resolution_updated = false;
    layer->visibility_updated = false;
    layer->appearance_updated = false;
    layer->visibility_constraints_updated = false;

    return render_request;
}
} // namespace

VectorTilesLayerSystem* create_system(PickingIdAllocator* picking_id_allocator)
{
    auto system = new VectorTilesLayerSystem();
    system->picking_id = picking::allocate_system_id(picking_id_allocator);
    system->heatmap_repr_registry = heatmaps::create_repr_registry();
    system->image_loader = vt::image_loader::create_loader();

    system->reprs.register_repr(
        hrz_proto::VectorReprType::NULL_VECTOR_REPR, vt::create_null_repr_system());
    system->reprs.register_repr(
        hrz_proto::VectorReprType::EXTRUDED_GEOMETRY_VECTOR_REPR,
        vt::create_extruded_repr_system());
    system->reprs.register_repr(
        hrz_proto::VectorReprType::FLAT_OVERLAY_VECTOR_REPR, vt::create_flat_overlay_repr_system());
    system->reprs.register_repr(
        hrz_proto::VectorReprType::MODEL_VECTOR_REPR, vt::create_model_repr_system());
    system->reprs.register_repr(
        hrz_proto::VectorReprType::CYLINDER_VECTOR_REPR, vt::create_cylinder_repr_system());
    system->reprs.register_repr(
        hrz_proto::VectorReprType::HEATMAP_VECTOR_REPR, vt::create_heatmap_repr_system());
    system->reprs.register_repr(
        hrz_proto::VectorReprType::SYMBOL_VECTOR_REPR, vt::create_symbol_repr_system());

    return system;
}

void destroy_system(
    VectorTilesLayerSystem* system,
    Render* render,
    VectorDataLoader* vector_data_loader,
    AssetsLoader* assets_loader,
    BlobAllocator* blob_allocator,
    JobScheduler* job_scheduler,
    FontRasterizer* font_rasterizer,
    SymbolCullingSystem* symbol_culling,
    PickingIdAllocator* id_allocator,
    SceneModel* model,
    hrz::PlanetSurface* planet)
{
    assert(system && planet);

    for (auto& pair : system->layer_ids_to_handles)
    {
        unregister_layer(system, pair.first);
    }
    _unregister_layers(system, model, blob_allocator, job_scheduler, planet);

    if (system->has_render_been_initialized)
    {
        system->reprs.deinit_render(render);
        system->has_render_been_initialized = false;
    }

    system->reprs.destroy(
        assets_loader, blob_allocator, job_scheduler, system->heatmap_repr_registry,
        system->image_loader, font_rasterizer, symbol_culling, render);

    picking::release_system_id(id_allocator, system->picking_id);
    heatmaps::destroy_repr_registry(system->heatmap_repr_registry);
    vt::image_loader::destroy_loader(system->image_loader);

    delete system;
}

void initialize_rendering(VectorTilesLayerSystem* system, Render* render)
{
    assert(system && render);
    assert(!system->has_render_been_initialized);

    system->reprs.init_render(render);
    system->has_render_been_initialized = true;
}

void notify_update(
    VectorTilesLayerSystem* system,
    uint64_t layer_id,
    scene_model::UpdateType update_type,
    const scene_model::VectorTilesLayerPath& path)
{
    assert(system);

    auto layer = _get_layer(system, layer_id);
    if (!layer) return;

    if (path.leaf())
    {
        layer->layer_updated = true;
        layer->bounds_updated = true;
        layer->clamping_updated = true;
        layer->style_script_updated = true;
        layer->special_attributes_updated = true;
        layer->attributes_updated = true;
        layer->palettes_updated = true;
        layer->rng_seed_updated = true;
        layer->resolution_updated = true;
        layer->visibility_constraints_updated = true;
        layer->appearance_updated = true;
        layer->reprs_array_sync.notify_model_update_all();
    }
    else if (path.is_source())
    {
        const auto path_source = path.clone().source();
        if (path_source.leaf())
        {
            layer->layer_updated = true;
            layer->bounds_updated = true;
        }
        else if (path_source.is_vector_data_layer_id())
        {
            layer->layer_updated = true;
        }
        else if (
            path_source.is_min_level() || path_source.is_max_level() || path_source.is_bounds())
        {
            layer->bounds_updated = true;
        }
    }
    else if (path.is_missing_tile_policy() || path.is_static_tiles())
    {
        layer->layer_updated = true;
    }
    else if (path.is_style())
    {
        const auto path_style = path.clone().style();
        if (path_style.leaf())
        {
            layer->style_script_updated = true;
            layer->special_attributes_updated = true;
            layer->attributes_updated = true;
            layer->palettes_updated = true;
            layer->rng_seed_updated = true;
            layer->reprs_array_sync.notify_model_update_all();
        }
        else if (path_style.is_attributes())
        {
            layer->attributes_updated = true;
        }
        else if (path_style.is_palettes())
        {
            layer->palettes_updated = true;
        }
        else if (path_style.is_rng_seed())
        {
            layer->rng_seed_updated = true;
        }
        else if (path_style.is_styling_script())
        {
            layer->style_script_updated = true;
        }
        else if (path_style.is_representations())
        {
            layer->reprs_array_sync.notify_model_update(update_type, path_style);
        }
        else if (
            path_style.is_anchor_z_attribute_name() || path_style.is_anchor_angle_attribute_name()
            || path_style.is_feature_type_attribute_name())
        {
            layer->special_attributes_updated = true;
        }
    }
    else if (path.is_resolution())
    {
        layer->resolution_updated = true;
    }
    else if (path.is_clamping())
    {
        layer->layer_updated = true;
        layer->clamping_updated = true;
    }
    else if (path.is_visible() || path.is_scene_views())
    {
        layer->visibility_updated = true;
    }
    else if (path.is_visibility_constraints())
    {
        layer->visibility_constraints_updated = true;
    }
    else if (path.is_clip_id() || path.is_lighting())
    {
        layer->appearance_updated = true;
    }
}

void register_layer(
    VectorTilesLayerSystem* system,
    SceneModel* model,
    hrz::PlanetSurface* planet,
    VectorDataLoader* vdl,
    ActorRunner* ar,
    uint64_t layer_id)
{
    assert(system && model && vdl);

    if (system->layer_ids_to_handles.count(layer_id) == 0)
    {
        auto layer_handle = system->layer_pool.alloc();
        auto layer = system->layer_pool.get_object(layer_handle);
        layer->id = layer_id;

        auto layer_picking_id = system->picking_id_pool.alloc();
        layer->picking_id = picking::combine_picking_ids(
            system->picking_id,
            hrz::vt::make_complementary_id_for_layer_picking_id(layer_picking_id));
        system->picking_id_to_layer_ids.insert({layer_picking_id, layer_id});
        layer->visibility_constraints_result = layers::MultiviewVisibilityConstraints();

        layer->vector_data_layer = 0;
        layer->missing_tile_policy = hrz_proto::MissingTilePolicy::USE_EMPTY_TILE;
        layer->static_tiles = false;
        layer->vt = vt::create(
            layer_id, layer->vector_data_layer, layer->missing_tile_policy, layer->static_tiles,
            layer->picking_id, planet, vdl, ar);

        system->layer_ids_to_handles.insert(std::make_pair(layer_id, layer_handle));

        hrz_proto::PathRoot root;
        root.mutable_vector_tiles_layer()->set_opaque(layer_id);
        scene_model::register_element(model, root);

        // Default data
        hrz_proto::VectorTilesLayer data;
        data.mutable_source()->set_vector_data_layer_id(0);
        data.mutable_source()->mutable_bounds()->set_west(-180);
        data.mutable_source()->mutable_bounds()->set_east(180);
        data.mutable_source()->mutable_bounds()->set_south(-90);
        data.mutable_source()->mutable_bounds()->set_north(90);
        data.mutable_resolution()->set_max_screen_space_error(2);
        data.set_visible(true);
        data.set_clip_id(-1);
        data.mutable_lighting()->set_enable_lighting(true);
        data.mutable_lighting()->set_cast_shadows(true);
        data.mutable_lighting()->set_receive_shadows(true);
        data.mutable_style()->set_rng_seed(0);
        data.mutable_scene_views()->set_bits((1u << SCENE_VIEW_COUNT) - 1u);

        hrz::SceneModelAccessor accessor(model);
        hrz_proto::VectorTilesLayerPathBuilder<hrz::SceneModelAccessor> builder(
            accessor, root.vector_tiles_layer());
        builder.set(data);
    }
}

void unregister_layer(VectorTilesLayerSystem* system, uint64_t layer_id)
{
    assert(system);
    system->unregistered_layers.push_back(layer_id);
}

const HeatmapReprRegistry* get_heatmap_repr_registry(VectorTilesLayerSystem* system)
{
    return system->heatmap_repr_registry;
}

RenderRequest work(
    VectorTilesLayerSystem* system,
    SceneModel* model,
    VectorDataLoader* vdl,
    AssetsLoader* al,
    BlobAllocator* ba,
    JobScheduler* js,
    ImageDecoder* imgdec,
    FontRasterizer* fr,
    SymbolCullingSystem* symbol_culling,
    AttributionRegistry* attributions,
    ActorRunner* ar,
    gsl::span<const RenderViewInfo> views_info,
    PlanetSurface* planet,
    const SelectionSystem* selection)
{
    HRZ_SCOPED_SAMPLE("vector tiles layers work");

    assert(system && model && vdl && al && ba && js && imgdec && fr && ar && planet);

    RenderRequest render_request;

    vt::image_loader::work(system->image_loader, al, ba, js);

    render_request |= _unregister_layers(system, model, ba, js, planet);

    for (auto it : system->layer_ids_to_handles)
    {
        auto layer = _get_layer(system, it.first);
        if (layer)
        {
            auto old_visibility_constraints_result = layer->visibility_constraints_result;

            layer->visibility_constraints_result = layers::are_visibility_constraints_satisfied(
                views_info, layer->visibility_constraints);

            if (old_visibility_constraints_result != layer->visibility_constraints_result)
            {
                render_request.request_visual_render();
                render_request.schedule_flat_overlay_render();
            }

            vt::set_visible_in(layer->vt, layer->visible_in());
        }
    }

    heatmaps::hide_all_layers(system->heatmap_repr_registry);

    for (auto it : system->layer_ids_to_handles)
    {
        render_request |= _update_layer(system, model, it.first, vdl, ba, js, planet, ar);

        auto layer = _get_layer(system, it.first);
        if (!layer)
        {
            continue;
        }

        // We have to work on invisible layers, otherwise in some cases the messages
        // queues can grow too large.
        // Working on invisible layers isn't too expensive anyway.
        auto work_render_request = vt::work(layer->vt, ba, js, &system->reprs, selection);

        if (layer->visible_in().any())
        {
            render_request |= work_render_request;
            heatmaps::make_layer_visible(system->heatmap_repr_registry, layer->id);
        }
    }

    render_request |= system->reprs.work(
        al, ba, js, system->heatmap_repr_registry, imgdec, system->image_loader, fr, symbol_culling,
        attributions, views_info);

    return render_request;
}

static Layer* _get_layer_from_picking(
    VectorTilesLayerSystem* system,
    uint8_t system_id,
    uint32_t complementary_id)
{
    if (system_id != system->picking_id) return nullptr;

    auto layer_picking_id = hrz::vt::layer_picking_id_from_complementary_id(complementary_id);
    auto it = system->picking_id_to_layer_ids.find(layer_picking_id);

    if (it == system->picking_id_to_layer_ids.end())
    {
        HRZ_LOG_ERROR("Cannot find vector tiles layer for picking id {}", layer_picking_id);
        return nullptr;
    }

    auto handle = system->layer_ids_to_handles.at(it->second);
    return system->layer_pool.get_object(handle);
}

void pick(
    VectorTilesLayerSystem* system,
    const picking::ObjectReference& obj,
    gsl::span<const std::pair<uint32_t, float>> heatmap_values,
    hrz_proto::PickResults& picking_results)
{
    hrz::flat_hash_map<uint64_t, hrz_proto::PickLayerResult*> layer_results;

    auto get_or_create_layer_result = [&](uint64_t layer_id) -> hrz_proto::PickLayerResult*
    {
        auto it = layer_results.find(layer_id);
        if (it != layer_results.end())
        {
            return it->second;
        }

        hrz_proto::PickLayerResult* result = picking_results.mutable_results()->Add();
        result->mutable_layer()->set_type(hrz_proto::LayerType::VECTOR_TILES);
        result->mutable_layer()->mutable_handle()->set_opaque(layer_id);

        layer_results.insert({layer_id, result});
        return result;
    };

    for (const auto& pair : heatmap_values)
    {
        const auto& info =
            heatmaps::get_repr_vector_tiles_info(system->heatmap_repr_registry, pair.first);

        auto* result = get_or_create_layer_result(info.layer_id);
        auto* heatmap = result->mutable_vector()->add_heatmaps();
        heatmap->set_repr_id(info.repr_id);
        heatmap->set_value(pair.second);
    }

    auto layer = _get_layer_from_picking(system, obj.system_id, obj.complementary_id);
    if (!layer || layer->visible_in().none())
    {
        return;
    }

    auto* result = get_or_create_layer_result(layer->id);
    vt::pick_feature(layer->vt, obj.complementary_id, obj.object_id, result);
}

std::pair<size_t, size_t> make_typed_object_references(
    VectorTilesLayerSystem* system,
    gsl::span<const picking::ObjectReference> objs,
    gsl::span<hrz_proto::TypedObjectReference> output)
{
    assert(objs.size() <= output.size());

    size_t in_cursor = 0;
    size_t out_cursor = 0;

    uint32_t last_complementary_id = 0;
    const Layer* layer = nullptr;

    // We assume that objs are sorted, thus if we find an object reference
    // without the correct system id, we assume there are no more inputs with
    // the same system id.
    while (in_cursor < objs.size() && objs[in_cursor].system_id == system->picking_id)
    {
        const auto& obj = objs[in_cursor++];

        if (!layer || last_complementary_id != obj.complementary_id)
        {
            layer = _get_layer_from_picking(system, obj.system_id, obj.complementary_id);
            last_complementary_id = obj.complementary_id;
        }

        if (layer && layer->visible_in().any())
        {
            auto* typed_obj = output[out_cursor++].mutable_vector_tiles();
            typed_obj->mutable_layer()->set_opaque(layer->id);
            auto feature_id =
                vt::get_feature_id_from_picking_id(layer->vt, obj.complementary_id, obj.object_id);
            if (feature_id.has_value() && !feature_id->is_null())
            {
                feature_id->to_proto(typed_obj->mutable_feature_id());
            }
        }
    }

    return std::make_pair(in_cursor, out_cursor);
}

std::optional<picking::FeatureReference> make_feature_picking_id(
    VectorTilesLayerSystem* system,
    const picking::ObjectReference& obj)
{
    assert(system);

    if (obj.system_id != system->picking_id)
    {
        return std::nullopt;
    }

    auto layer = _get_layer_from_picking(system, obj.system_id, obj.complementary_id);

    if (layer == nullptr)
    {
        return std::nullopt;
    }

    auto feature_id =
        vt::get_feature_id_from_picking_id(layer->vt, obj.complementary_id, obj.object_id);

    if (!feature_id.has_value() || feature_id->is_null())
    {
        return std::nullopt;
    }

    picking::FeatureReference ref;
    ref.feature_id_hash = feature_id->hash();
    ref.complementary_id = hrz::vt::make_complementary_id_for_layer_picking_id(
        hrz::vt::layer_picking_id_from_complementary_id(obj.complementary_id));
    ref.system_id = obj.system_id;

    return ref;
}

RenderRequest work_gpu(
    VectorTilesLayerSystem* system,
    Render* render,
    BlobAllocator* ba,
    SymbolCullingSystem* culling,
    gsl::span<const RenderViewInfo> views_info)
{
    assert(system && ba && render);

    vt::image_loader::work_gpu(system->image_loader, render);

    for (auto& pair : system->layer_ids_to_handles)
    {
        auto layer = system->layer_pool.get_object(pair.second);

        vt::work_gpu(layer->vt, render, views_info);
    }

    auto render_request = system->reprs.work_gpu(render, ba, system->image_loader, culling);

    return render_request;
}

void draw(
    VectorTilesLayerSystem* system,
    Render* render,
    const RenderRequest& render_request,
    gsl::span<const RenderViewInfo> views_info,
    SymbolCullingSystem* symbol_culling,
    AttributionRegistry* attributions)
{
    assert(system && render);

    for (auto& pair : system->layer_ids_to_handles)
    {
        auto layer = system->layer_pool.get_object(pair.second);

        if (layer->visible_in().any())
        {
            vt::draw(
                layer->vt, render, render_request, &system->reprs, views_info, symbol_culling,
                attributions);
        }
    }

    system->reprs.draw(render, attributions);
}

bool is_working(VectorTilesLayerSystem* system)
{
    assert(system);
    if (!system->has_render_been_initialized)
    {
        return true;
    }

    for (auto& it : system->layer_ids_to_handles)
    {
        auto layer = _get_layer(system, it.first);
        if (!layer || layer->visible_in().none()) continue;

        if (vt::is_working(layer->vt)) return true;
    }

    return false;
}

void dev_ui(
    VectorTilesLayerSystem* system,
    const LayersInfo* layers_info,
    mu_Context* ctx,
    const char* window_name)
{
    assert(system);

    if (mu_begin_window_ex(ctx, window_name, mu_rect(300, 200, 400, 370), MU_OPT_CLOSED))
    {
        fmt::memory_buffer buffer;

        for (auto pair : system->layer_ids_to_handles)
        {
            const char* layer_name = "<Unknown>";

            auto info_it = layers_info->layers.find(pair.first);
            if (info_it != layers_info->layers.end())
            {
                layer_name = info_it->second.name.c_str();
            }

            if (mu_header_ex(ctx, hrz::format_to_buffer(buffer, "{}", pair.first), layer_name, 0))
            {
                auto* layer = system->layer_pool.get_object(pair.second);
                assert(layer);

                vt::dev_ui(layer->vt, ctx);
            }
        }

        mu_end_window(ctx);
    }
}

} // namespace vector_tiles_layers
} // namespace hrz
