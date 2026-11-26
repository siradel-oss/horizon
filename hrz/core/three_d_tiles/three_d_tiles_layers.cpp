// SPDX-FileCopyrightText: Copyright 2020 Siradel
// SPDX-License-Identifier: MIT

#include "hrz/core/three_d_tiles/three_d_tiles_layers.h"

#include "hrz/common/proto_geo.h"
#include "hrz/common/proto_maths.h"
#include "hrz/core/assets_loader/assets_loader.h"
#include "hrz/core/picking_id_allocator.h"
#include "hrz/core/render/context.h"
#include "hrz/core/render/lighting_settings.h"
#include "hrz/core/scene.h"
#include "hrz/core/scene_model_array_sync.h"
#include "hrz/core/selection.h"
#include "hrz/core/style/script.h"
#include "hrz/core/three_d_tiles/system.h"
#include "hrz/core/three_d_tiles/tile_types.h"
#include "hrz/core/vector/data_loader/data_loader.h"
#include "hrz/core/visibility_constraints.h"
#include "hrz/fnd/flat_hash_map.h"
#include "hrz/fnd/gen_object_pool.h"
#include "hrz/fnd/log.h"
#include "hrz/protocol/path_builder/layer/three_d_tiles_layer.h"

#include <cassert>
#include <optional>
#include <span>
#include <string>
#include <vector>

using namespace hrz::three_d_tiles;

namespace
{

struct ArraySyncTraits
{
    enum ElementUpdateType
    {
        All,
        Palette,
    };

    using ContainerPath = hrz::scene_model::ThreeDTilesLayerPath;
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

struct Layer
{
    uint64_t global_id;

    TilesetH tileset;

    bool source_updated = false;
    bool transform_updated = false;
    bool max_screen_space_error_updated = false;
    bool refinement_hysteresis_updated = false;
    bool id_attribute_name_updated = false;
    bool special_attributes_updated = false;
    bool vector_data_layer_id_updated = false;
    bool attributes_updated = false;
    bool palettes_updated = false;
    bool rng_seed_updated = false;
    bool styling_script_updated = false;
    bool visibility_updated = false;
    bool loading_priority_updated = false;
    bool visibility_constraints_updated = false;
    bool scene_views_updated = false;
    bool appearance_updated = false;
    bool http_headers_updated = false;
    bool material_properties_updated = false;
    bool active_materials_updated = false;

    using ArraySync = hrz::scene_model::ArraySync<ArraySyncTraits>;

    // Number of materials currently in the 3DTiles.
    size_t material_count = 0;
    ArraySync materials_array_sync;
};

} // namespace

namespace hrz
{

struct ThreeDTilesLayerSystem
{
    using IndexPool = GenIndexPool<LayerH, 8, 16>;
    using LayerPool = GenObjectPool<Layer, IndexPool, 64>;

    LayerPool layer_pool;
    hrz::flat_hash_map<uint64_t, LayerH> global_layer_id_to_handle;
    hrz::flat_hash_map<LayerH, uint64_t> layer_handles_to_global_id;

    std::vector<uint64_t> unregistered_layers;

    uint8_t system_picking_id;

    ThreeDTilesSystem three_d_tiles;

    Layer* get_layer(uint64_t global_layer_id)
    {
        const auto it = global_layer_id_to_handle.find(global_layer_id);
        if (it == global_layer_id_to_handle.end())
        {
            return nullptr;
        }
        return layer_pool.get_object(it->second);
    }
};

namespace three_d_tiles_layers
{
namespace
{

void unregister_layers(
    ThreeDTilesLayerSystem* system,
    SceneModel* model,
    AssetsLoader* al,
    JobScheduler* js)
{
    assert(system && model && al && js);

    for (const auto global_layer_id : system->unregistered_layers)
    {
        const auto it = system->global_layer_id_to_handle.find(global_layer_id);
        if (it != system->global_layer_id_to_handle.end())
        {
            hrz_proto::PathRoot root;
            root.mutable_three_d_tiles_layer()->set_opaque(global_layer_id);
            scene_model::unregister_element(model, root);

            const auto layer_handle = it->second;
            auto layer = system->layer_pool.get_object(layer_handle);

            if (layer)
            {
                if (layer->tileset != 0)
                {
                    system->three_d_tiles.mark_tileset_to_unload(layer->tileset);
                }

                system->layer_pool.release(layer_handle);
            }

            system->global_layer_id_to_handle.erase(it);
            system->layer_handles_to_global_id.erase(layer_handle);
        }
    }

    system->unregistered_layers.clear();
}

RenderRequest update_layer(
    ThreeDTilesLayerSystem* system,
    uint64_t global_layer_id,
    SceneModel* model,
    const SelectionSystem* selection,
    AssetsLoader* al,
    AttributionRegistry* attributions)
{
    RenderRequest render_request;

    auto layer = system->get_layer(global_layer_id);
    if (!layer) return render_request;

    hrz_proto::LayerHandle scene_model_handle;
    scene_model_handle.set_opaque(global_layer_id);

    hrz_proto::ThreeDTilesLayerPathBuilder<hrz::SceneModelAccessor> builder(
        model, scene_model_handle);

    const LayerH internal_layer_handle = system->global_layer_id_to_handle.at(global_layer_id);

    bool reload_tileset = layer->source_updated || layer->transform_updated
        || layer->id_attribute_name_updated || layer->attributes_updated
        || layer->special_attributes_updated;

    if (layer->http_headers_updated && !reload_tileset)
    {
        const auto headers = hrz::assets_loader::from_proto(builder.clone().http_headers().get());
        if (system->three_d_tiles.set_http_headers(layer->tileset, headers))
        {
            layer->source_updated = true;
            reload_tileset = true;
        }
    }

    if (reload_tileset)
    {
        if (layer->tileset != 0)
        {
            system->three_d_tiles.mark_tileset_to_unload(layer->tileset);
        }

        auto new_model = builder.clone().get();

        const auto get_transform = [&]()
        {
            const auto transform_matrix = to_lm(new_model.transform());
            const auto geo_matrix = new_model.apply_geographic_position()
                ? enu_to_ecef_transform_for_geo(from_proto(new_model.geographic_position()))
                : lm::dmat4::identity();
            return geo_matrix * transform_matrix;
        };

        const auto& new_url = new_model.url();
        const auto& headers = hrz::assets_loader::from_proto(new_model.http_headers());
        const auto attribution =
            attribution::register_attribution(attributions, {new_model.attribution(), ""});
        const bool preserve_query_parameters = new_model.preserve_query_parameters();
        const auto transform = get_transform();
        const double max_screen_space_error = new_model.max_screen_space_error();
        const double refinement_hysteresis = new_model.refinement_hysteresis();
        const bool is_visible = new_model.visible();
        const int8_t loading_priority = clamp_cast<int32_t, int8_t>(new_model.loading_priority());
        const int32_t clip_id = new_model.clip_id();
        const uint32_t scene_views_bitset = new_model.scene_views().bits();
        const hrz::render::LightingSettings lighting_settings =
            hrz::render::from_proto(new_model.lighting());
        const hrz_proto::LayerVisibilityConstraintList visibility_constraints =
            new_model.visibility_constraints();
        const bool draw_under_flat_overlays = new_model.draw_under_flat_overlays();
        const uint32_t color_blend_mode =
            new_model.material_properties().feature_color_blend_mode();
        const float color_blend_strength =
            new_model.material_properties().feature_color_blend_strength();

        const unsigned int attribute_count = new_model.attributes_size();
        std::vector<hrz::three_d_tiles::AttributeConfig> attributes;
        attributes.reserve(attribute_count);

        for (unsigned int i = 0; i < attribute_count; ++i)
        {
            const auto& attribute = new_model.attributes(i);

            switch (attribute.source())
            {
                case hrz_proto::ThreeDTileAttributeSource::BATCH_TABLE_SOURCE:
                    attributes.push_back(
                        {std::string{attribute.name()},
                         hrz::three_d_tiles::AttributeConfig::BatchTable{attribute.is_feature_id()},
                         attribute.vector_data_attr_id(), style::Parser::INSERT_ERROR,
                         attribute.transform()});
                    break;
                case hrz_proto::ThreeDTileAttributeSource::VECTOR_DATA_LAYER_SOURCE:
                    attributes.push_back(
                        {std::string{attribute.name()},
                         hrz::three_d_tiles::AttributeConfig::VectorDataLayer{},
                         attribute.vector_data_attr_id(), style::Parser::INSERT_ERROR,
                         attribute.transform()});
                    break;
                case hrz_proto::ThreeDTileAttributeSource::BATCH_CLASS_ID_SOURCE:
                    attributes.push_back(
                        {std::string{attribute.name()},
                         hrz::three_d_tiles::AttributeConfig::BatchClassId{
                             attribute.is_feature_id()
                         },
                         attribute.vector_data_attr_id(), style::Parser::INSERT_ERROR,
                         attribute.transform()});
                    break;
                case hrz_proto::ThreeDTileAttributeSource::BATCH_CLASS_NAME_SOURCE:
                    attributes.push_back(
                        {std::string{attribute.name()},
                         hrz::three_d_tiles::AttributeConfig::BatchClassName{
                             attribute.is_feature_id()
                         },
                         attribute.vector_data_attr_id(), style::Parser::INSERT_ERROR,
                         attribute.transform()});
                    break;
                default: assert(false && "Unhandled case"); break;
            }
        }

        layer->tileset = system->three_d_tiles.add_tileset(
            new_url, headers, attribution, preserve_query_parameters, transform,
            max_screen_space_error, refinement_hysteresis, attributes, internal_layer_handle,
            global_layer_id, is_visible, loading_priority, clip_id, scene_views_bitset,
            color_blend_mode, color_blend_strength, lighting_settings, visibility_constraints,
            draw_under_flat_overlays, al);
    }

    if (reload_tileset || layer->vector_data_layer_id_updated)
    {
        system->three_d_tiles.set_vector_data_layer_id(
            layer->tileset, builder.clone().vector_data_layer_id().get());
    }

    if (reload_tileset || layer->styling_script_updated || layer->palettes_updated
        || layer->rng_seed_updated)
    {
        const auto layer_data = builder.clone().get();

        // @Todo It would be nice to have a way to access arrays without
        // having to get the parent object.

        system->three_d_tiles.set_style(
            layer->tileset, layer_data.styling_script(),
            {layer_data.palettes().data(), (size_t)layer_data.palettes_size()},
            layer_data.rng_seed());
    }

    if (reload_tileset || layer->max_screen_space_error_updated)
    {
        if (layer->tileset != 0)
        {
            const double max_screen_space_error = builder.clone().max_screen_space_error().get();
            system->three_d_tiles.set_max_screen_space_error(
                layer->tileset, max_screen_space_error);
        }
    }

    if (reload_tileset || layer->refinement_hysteresis_updated)
    {
        if (layer->tileset != 0)
        {
            const double refinement_hysteresis = builder.clone().refinement_hysteresis().get();
            system->three_d_tiles.set_refinement_hysteresis(layer->tileset, refinement_hysteresis);
        }
    }

    if (reload_tileset || layer->visibility_updated)
    {
        const bool is_visible = builder.clone().visible().get();
        system->three_d_tiles.set_visibility(layer->tileset, is_visible, al);
        render_request.request_visual_render();
    }

    if (reload_tileset || layer->loading_priority_updated)
    {
        const int8_t loading_priority =
            clamp_cast<int32_t, int8_t>(builder.clone().loading_priority().get());
        system->three_d_tiles.set_loading_priority(layer->tileset, loading_priority);
    }

    if (reload_tileset || layer->scene_views_updated)
    {
        system->three_d_tiles.set_scene_views_bitset(
            layer->tileset, builder.clone().scene_views().bits().get());
        render_request.request_visual_render();
    }

    if (reload_tileset || layer->appearance_updated)
    {
        const int32_t clip_id = builder.clone().clip_id().get();
        const hrz::render::LightingSettings lighting_settings =
            hrz::render::from_proto(builder.clone().lighting().get());
        const bool draw_under_flat_overlays = builder.clone().draw_under_flat_overlays().get();

        system->three_d_tiles.update_appearance(
            layer->tileset, clip_id, lighting_settings, draw_under_flat_overlays);
        render_request.request_visual_render();
    }

    if (reload_tileset || layer->visibility_constraints_updated)
    {
        const hrz_proto::LayerVisibilityConstraintList visibility_constraints =
            builder.clone().visibility_constraints().get();
        system->three_d_tiles.set_visibility_constraints(layer->tileset, visibility_constraints);
        render_request.request_visual_render();
    }

    bool reload_subtiles_content = false;

    auto recreate_all_materials = [&]()
    {
        // There is no way to retrieve an entire array _only_ so... Anyway it's
        // not like we're supposed to do this every frame.
        const auto layer_proto = builder.clone().get();

        system->three_d_tiles.recreate_all_materials(
            layer->tileset,
            std::span<const hrz_proto::Material* const>(
                layer_proto.materials().data(), layer_proto.materials().size()));

        reload_subtiles_content = true;
        layer->material_count = layer_proto.materials_size();
    };

    if (reload_tileset)
    {
        recreate_all_materials();
        layer->materials_array_sync.reset(layer->material_count);
    }
    else
    {
        layer->materials_array_sync.synchronize(
            [&](const Layer::ArraySync::Command& cmd) -> size_t
            {
                switch (cmd.type)
                {
                    case Layer::ArraySync::Command::Add:
                    {
                        const auto material =
                            builder.clone().materials((uint32_t)cmd.info.add.index_auth).get();
                        system->three_d_tiles.add_material(layer->tileset, material);
                        reload_subtiles_content = true;
                        layer->material_count += 1;
                        break;
                    }
                    case Layer::ArraySync::Command::RebuildAll:
                    {
                        recreate_all_materials();
                        break;
                    }
                    case Layer::ArraySync::Command::Remove:
                    {
                        system->three_d_tiles.remove_material(
                            layer->tileset, cmd.info.remove.index_mirror);
                        reload_subtiles_content = true;
                        layer->material_count -= 1;
                        break;
                    }
                    case Layer::ArraySync::Command::Update:
                    {
                        switch (cmd.info.update.update_type)
                        {
                            case ArraySyncTraits::ElementUpdateType::All:
                            {
                                const auto material =
                                    builder.clone()
                                        .materials((uint32_t)cmd.info.update.index_auth)
                                        .get();
                                system->three_d_tiles.update_material(
                                    layer->tileset, cmd.info.update.index_mirror, material);
                                reload_subtiles_content = true;
                                break;
                            }
                            case ArraySyncTraits::ElementUpdateType::Palette:
                            {
                                const auto palette =
                                    builder.clone()
                                        .materials((uint32_t)cmd.info.update.index_auth)
                                        .data_texture_palette()
                                        .get();
                                system->three_d_tiles.update_material_palette(
                                    layer->tileset, cmd.info.update.index_mirror, palette);
                                render_request.request_visual_render();
                                break;
                            }
                        }
                        break;
                    }
                }
                return layer->material_count;
            });
    }

    if (reload_tileset || layer->material_properties_updated)
    {
        const auto prps = builder.clone().material_properties().get();
        system->three_d_tiles.update_material_properties(layer->tileset, prps);
        render_request.request_visual_render();
        layer->material_properties_updated = false;
    }

    if (reload_tileset || layer->active_materials_updated)
    {
        const auto prps = builder.clone().material_properties().get();
        system->three_d_tiles.update_active_materials(
            layer->tileset, prps.base_material(),
            prps.enable_overlay() ? prps.overlay_material() : std::optional<std::string_view>{});
        reload_subtiles_content = true;
        layer->active_materials_updated = false;
    }

    if (reload_tileset || reload_subtiles_content)
    {
        system->three_d_tiles.reload_subtiles_content(layer->tileset);
    }

    // There is no function to update the root transform of the tileset
    // without reloading it. Root transforms are rarely used, especially
    // for big tilesets, and changing the transform for an existing layer
    // even less.
    // As it would be a bother to implement root transform updates, I have
    // just taken the easy way.
    //      -tpetillon, 2020-10-09

    layer->source_updated = false;
    layer->transform_updated = false;
    layer->max_screen_space_error_updated = false;
    layer->refinement_hysteresis_updated = false;
    layer->id_attribute_name_updated = false;
    layer->special_attributes_updated = false;
    layer->vector_data_layer_id_updated = false;
    layer->attributes_updated = false;
    layer->palettes_updated = false;
    layer->rng_seed_updated = false;
    layer->styling_script_updated = false;
    layer->visibility_updated = false;
    layer->loading_priority_updated = false;
    layer->visibility_constraints_updated = false;
    layer->scene_views_updated = false;
    layer->appearance_updated = false;
    layer->http_headers_updated = false;
    layer->active_materials_updated = false;
    layer->material_properties_updated = false;

    if (reload_tileset
        || (hrz::selection::has_changed_since_last_frame(selection)
            && hrz::selection::has_changed_since_last_frame(selection, global_layer_id)))
    {
        const size_t count = hrz::selection::selected_objects_count(selection, global_layer_id);
        std::vector<vector_data::FeatureIdHash> selected_feature_ids(count);
        hrz::selection::get_selected_objects(
            selection, global_layer_id,
            std::span<vector_data::FeatureIdHash>(selected_feature_ids));

        system->three_d_tiles.clear_selected_features(layer->tileset);
        system->three_d_tiles.add_selected_features(layer->tileset, selected_feature_ids);
        render_request.request_visual_render();
    }

    return render_request;
}

} // namespace

ThreeDTilesLayerSystem* create_system(
    PickingIdAllocator* picking_id_allocator,
    VectorDataLoader* vdl)
{
    auto system = new ThreeDTilesLayerSystem();
    system->system_picking_id = picking::allocate_system_id(picking_id_allocator);
    system->three_d_tiles.init(system->system_picking_id, vector_data::create_channel(vdl));

    return system;
}

void destroy_system(
    ThreeDTilesLayerSystem* system,
    AssetsLoader* al,
    JobScheduler* js,
    BlobAllocator* ba,
    Render* render,
    PickingIdAllocator* pia,
    SceneModel* scene_model)
{
    assert(system && al && js && ba && render && pia && scene_model);

    for (auto [global_layer_id, _] : system->global_layer_id_to_handle)
    {
        unregister_layer(system, global_layer_id);
    }
    unregister_layers(system, scene_model, al, js);

    system->three_d_tiles.destroy(al, js, ba, render);

    picking::release_system_id(pia, system->system_picking_id);

    delete system;
}

void initialize_rendering(ThreeDTilesLayerSystem* system, Render* render)
{
    assert(system && render);

    system->three_d_tiles.initialize_rendering(render);
}

void register_layer(ThreeDTilesLayerSystem* system, SceneModel* model, uint64_t global_layer_id)
{
    assert(system && model);

    if (system->global_layer_id_to_handle.count(global_layer_id) == 0)
    {
        const auto layer_handle = system->layer_pool.alloc();
        auto layer = system->layer_pool.get_object(layer_handle);
        layer->global_id = global_layer_id;
        layer->tileset = 0;

        system->global_layer_id_to_handle.insert({global_layer_id, layer_handle});
        system->layer_handles_to_global_id.insert({layer_handle, global_layer_id});

        hrz_proto::PathRoot root;
        root.mutable_three_d_tiles_layer()->set_opaque(global_layer_id);
        scene_model::register_element(model, root);

        // Default data
        hrz_proto::ThreeDTilesLayer data;
        data.set_url("");
        data.set_visible(true);
        data.set_max_screen_space_error(16.0F);
        data.set_refinement_hysteresis(0.3F);

        auto* transform = data.mutable_transform();
        transform->mutable_scale()->set_x(1.0F);
        transform->mutable_scale()->set_y(1.0F);
        transform->mutable_scale()->set_z(1.0F);
        transform->mutable_rotation()->set_w(1.0F);
        transform->mutable_frame()->set_front(hrz_proto::Axis::POS_Y);
        transform->mutable_frame()->set_up(hrz_proto::Axis::POS_Z);
        transform->mutable_frame()->set_handedness(hrz_proto::Handedness::RIGHT);

        data.set_apply_geographic_position(false);
        data.set_vector_data_layer_id(0);
        data.set_rng_seed(0);
        data.set_styling_script("");
        data.mutable_scene_views()->set_bits((1U << hrz::SCENE_VIEW_COUNT) - 1);
        data.set_clip_id(-1);
        data.mutable_lighting()->set_enable_lighting(true);
        data.mutable_lighting()->set_cast_shadows(true);
        data.mutable_lighting()->set_receive_shadows(true);
        data.mutable_material_properties()->set_feature_color_blend_mode(hrz_proto::BLEND_MULTIPLY);
        data.mutable_material_properties()->set_feature_color_blend_strength(1.0);

        hrz_proto::ThreeDTilesLayerPathBuilder<hrz::SceneModelAccessor>(
            model, root.three_d_tiles_layer())
            .set(data);
    }
}

void unregister_layer(ThreeDTilesLayerSystem* system, uint64_t layer_id)
{
    assert(system);

    system->unregistered_layers.push_back(layer_id);
}

void notify_update(
    ThreeDTilesLayerSystem* system,
    uint64_t layer_id,
    scene_model::UpdateType update_type,
    const scene_model::ThreeDTilesLayerPath& path)
{
    auto layer = system->get_layer(layer_id);
    if (!layer) return;

    layer->source_updated |= path.leaf() || path.is_url() || path.is_preserve_query_parameters()
        || path.is_attribution();
    layer->transform_updated |= path.leaf() || path.is_transform() || path.is_geographic_position()
        || path.is_apply_geographic_position();
    layer->max_screen_space_error_updated |= path.leaf() || path.is_max_screen_space_error();
    layer->refinement_hysteresis_updated |= path.leaf() || path.is_refinement_hysteresis();
    layer->id_attribute_name_updated |= path.leaf();
    layer->special_attributes_updated |= path.leaf();
    layer->vector_data_layer_id_updated |= path.leaf() || path.is_vector_data_layer_id();
    layer->attributes_updated |=
        path.leaf() || path.is_vector_data_layer_id() || path.is_attributes();
    layer->palettes_updated |= path.leaf() || path.is_palettes();
    layer->rng_seed_updated |= path.leaf() || path.is_rng_seed();
    layer->styling_script_updated |= path.leaf() || path.is_styling_script();
    layer->visibility_updated |= path.leaf() || path.is_visible();
    layer->loading_priority_updated |= path.leaf() || path.is_loading_priority();
    layer->scene_views_updated |= path.leaf() || path.is_scene_views();
    layer->appearance_updated |= path.leaf() || path.is_clip_id() || path.is_lighting()
        || path.is_draw_under_flat_overlays();
    layer->visibility_constraints_updated |= path.leaf() || path.is_visibility_constraints();
    layer->http_headers_updated |= path.leaf() || path.is_http_headers();
    layer->material_properties_updated = path.is_material_properties() || path.leaf();

    if (path.is_material_properties())
    {
        const auto prps_path = path.clone().material_properties();
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

    if (path.leaf() || path.is_materials())
    {
        layer->materials_array_sync.notify_model_update(update_type, path);
    }
}

RenderRequest work(
    ThreeDTilesLayerSystem* system,
    SceneModel* model,
    const SelectionSystem* selection,
    AssetsLoader* al,
    JobScheduler* js,
    BlobAllocator* ba,
    ImageDecoder* imgdec,
    AttributionRegistry* attributions,
    std::span<const RenderViewInfo> views_info)
{
    assert(system && model && selection && al && js && ba && imgdec);

    RenderRequest render_request;

    unregister_layers(system, model, al, js);

    for (auto [global_layer_id, _] : system->global_layer_id_to_handle)
    {
        render_request |= update_layer(system, global_layer_id, model, selection, al, attributions);
    }

    render_request |= system->three_d_tiles.work(al, js, ba, imgdec, attributions, views_info);

    return render_request;
}

bool is_working(ThreeDTilesLayerSystem* system)
{
    return system->three_d_tiles.is_working();
}

namespace
{

struct PickingInfo
{
    uint64_t layer_id;
    std::optional<hrz::vector_data::FeatureId> feature_id;

    TilesetH tileset_handle;
    uint32_t tile_index;
    uint32_t batch_id;
};

std::optional<PickingInfo> get_picking_info(
    ThreeDTilesLayerSystem* system,
    const picking::ObjectReference& ref)
{
    if (ref.system_id != system->system_picking_id) return std::nullopt;

    TilesetH tileset_handle = 0;
    uint32_t tile_index = 0;
    uint32_t batch_id = 0;

    ThreeDTilesSystem::extract_info_from_object_reference(
        ref, &tileset_handle, &tile_index, &batch_id);

    const LayerH layer_handle = system->three_d_tiles.get_layer_for_tileset(tileset_handle);

    if (tileset_handle == 0)
    {
        HRZ_LOG_ERROR("Cannot find 3D Tiles layer for tileset {}", tileset_handle);
        return std::nullopt;
    }

    if (!system->three_d_tiles.is_tileset_visible(tileset_handle))
    {
        return std::nullopt;
    }

    const uint64_t global_layer_id = system->layer_handles_to_global_id.at(layer_handle);
    const auto feature_id =
        system->three_d_tiles.get_feature_id(tileset_handle, tile_index, batch_id);

    return {{global_layer_id, feature_id, tileset_handle, tile_index, batch_id}};
}

} // namespace

void pick(
    ThreeDTilesLayerSystem* system,
    const picking::PositionResult& result_raw,
    hrz_proto::PickResults& picking_results)
{
    assert(system);
    assert(result_raw.position.has_value());

    auto info_opt = get_picking_info(system, result_raw.ref);

    if (!info_opt.has_value()) return;

    auto& info = info_opt.value();

    auto result = picking_results.add_results();
    result->mutable_layer()->set_type(hrz_proto::LayerType::THREE_D_TILES);
    result->mutable_layer()->mutable_handle()->set_opaque(info.layer_id);
    if (info.feature_id.has_value() && !info.feature_id->is_null())
    {
        info.feature_id->to_proto(result->mutable_three_d_tile()->mutable_feature_id());
    }
    result->mutable_three_d_tile()->mutable_position()->set_x(result_raw.position.value().x);
    result->mutable_three_d_tile()->mutable_position()->set_y(result_raw.position.value().y);
    result->mutable_three_d_tile()->mutable_position()->set_z(result_raw.position.value().z);
    result->mutable_three_d_tile()->set_tile_url(
        system->three_d_tiles.get_url_for_tile(info.tileset_handle, info.tile_index));
    result->mutable_three_d_tile()->set_data_texture_value(result_raw.data_texture_value);

    if (!info.feature_id.has_value() || info.feature_id->is_null())
    {
        return;
    }

    const auto attribute_count = system->three_d_tiles.get_attribute_count(info.tileset_handle);
    for (unsigned int i = 0; i < attribute_count; ++i)
    {
        auto value_opt = system->three_d_tiles.get_attribute_value(
            info.tileset_handle, info.tile_index, info.batch_id, i);

        if (!value_opt.has_value()) continue;

        const auto& value = value_opt.value();

        result->mutable_three_d_tile()->add_names(value.name);
        *result->mutable_three_d_tile()->add_values() = value.value;
    }
}

std::pair<size_t, size_t> make_typed_object_references(
    ThreeDTilesLayerSystem* system,
    std::span<const picking::ObjectReference> objs,
    std::span<hrz_proto::TypedObjectReference> output)
{
    assert(objs.size() <= output.size());

    size_t in_cursor = 0;
    size_t out_cursor = 0;

    TilesetH tileset_handle = 0;
    LayerH layer_handle = 0;
    uint64_t layer_id = 0;
    bool is_visible = false;

    while (in_cursor < objs.size())
    {
        const auto& obj = objs[in_cursor];
        if (obj.system_id == system->system_picking_id)
        {
            TilesetH new_tileset_handle = 0;
            uint32_t tile_index = 0;
            uint32_t batch_id = 0;

            ThreeDTilesSystem::extract_info_from_object_reference(
                obj, &new_tileset_handle, &tile_index, &batch_id);

            if (new_tileset_handle != tileset_handle)
            {
                tileset_handle = new_tileset_handle;
                if (tileset_handle != 0)
                {
                    layer_handle = system->three_d_tiles.get_layer_for_tileset(tileset_handle);
                    is_visible = system->three_d_tiles.is_tileset_visible(tileset_handle);
                    layer_id = system->layer_handles_to_global_id.at(layer_handle);
                }
            }

            if (is_visible && tileset_handle != 0)
            {
                auto feature_id =
                    system->three_d_tiles.get_feature_id(tileset_handle, tile_index, batch_id);

                auto ref = output[out_cursor++].mutable_three_d_tiles();
                ref->mutable_layer()->set_opaque(layer_id);
                if (feature_id.has_value() && !feature_id->is_null())
                {
                    feature_id->to_proto(ref->mutable_feature_id());
                }
            }

            in_cursor++;
        }
        else
        {
            // We assume that objs are sorted, thus if we find an object
            // reference without the correct system id, we assume there are no
            // more inputs with the same system id.
            break;
        }
    }

    return std::make_pair(in_cursor, out_cursor);
}

std::optional<picking::FeatureReference> make_feature_reference(
    ThreeDTilesLayerSystem* system,
    const picking::ObjectReference& obj)
{
    assert(system);

    if (obj.system_id != system->system_picking_id)
    {
        return std::nullopt;
    }

    TilesetH tileset_handle = 0;
    uint32_t tile_index = 0;
    uint32_t batch_id = 0;

    ThreeDTilesSystem::extract_info_from_object_reference(
        obj, &tileset_handle, &tile_index, &batch_id);

    if (tileset_handle != 0)
    {
        const LayerH layer_handle = system->three_d_tiles.get_layer_for_tileset(tileset_handle);
        const Layer* layer = system->layer_pool.get_object(layer_handle);

        if (layer)
        {
            auto feature_id =
                system->three_d_tiles.get_feature_id(tileset_handle, tile_index, batch_id);

            if (feature_id.has_value() && !feature_id->is_null())
            {
                picking::FeatureReference ref;
                ref.system_id = system->system_picking_id;
                ref.complementary_id = layer_handle;
                ref.feature_id_hash = feature_id.value().hash();
                return ref;
            }
        }
    }

    return std::nullopt;
}

RenderRequest work_gpu(ThreeDTilesLayerSystem* system, Render* render, BlobAllocator* ba)
{
    assert(system && render && ba);

    return system->three_d_tiles.work_gpu(render, ba);
}

void draw(ThreeDTilesLayerSystem* system, Render* render, AttributionRegistry* attributions)
{
    assert(system && render);

    system->three_d_tiles.draw(render, attributions);
}

} // namespace three_d_tiles_layers
} // namespace hrz
